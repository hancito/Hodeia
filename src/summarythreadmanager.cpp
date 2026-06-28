#include "headers/summarythreadmanager.h"
#include "headers/database.h"

#include <QDebug>

SummaryController::SummaryController(QObject *parent)
    : QObject(parent), m_config(config) {}

SummaryController::~SummaryController() { stopSummarize(); }

void SummaryController::startSummarize() {
  // Clean up any leftover objects from a previous run
  if (m_thread && m_thread->isRunning()) {
    return;
  }
  if (m_worker) {
    m_worker->deleteLater();
    m_worker = nullptr;
  }
  if (m_thread) {
    m_thread->deleteLater();
    m_thread = nullptr;
  }

  m_thread = new QThread();
  m_worker = new Summarizer(m_config);
  m_worker->moveToThread(m_thread);

  // Trigger work
  connect(m_thread, &QThread::started, m_worker,
          [this]() { m_worker->summarizePapers(); });

  // When done: quit thread, then delete objects in the main thread's event loop
  connect(m_worker, &Summarizer::finished, m_thread, &QThread::quit);
  connect(m_thread, &QThread::finished, this, [this]() {
    m_worker->deleteLater();
    m_thread->deleteLater();
    m_worker = nullptr;
    m_thread = nullptr;
  });

  // Forward signals to QML
  connect(m_worker, &Summarizer::started, this,
          &SummaryController::summaryStarted);

  connect(m_worker, &Summarizer::finished, this,
          &SummaryController::summaryFinished());

  qInfo() << "[EngineController] Starting worker thread";
  m_thread->start();
}

void SummaryController::stopSummarize() {
  if (m_worker && m_thread && m_thread->isRunning()) {
    m_worker->stop();
    m_thread->quit();
    m_thread->wait();
  }
}
