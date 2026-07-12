#include "headers/summarythreadmanager.h"
#include "headers/database.h"

#include <QDebug>

SummaryController::SummaryController(ConfigManager *config,
                                     DatabaseManager *database, QObject *parent)
    : QObject(parent), m_config(config), m_database(database) {}

SummaryController::~SummaryController() { stopSummarize(); }

void SummaryController::startSummarize(const QStringList &hashes) {
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

  // Save parameters
  m_hashes = hashes;

  m_thread = new QThread();
  m_worker = new Summarizer(m_config, m_database);
  m_worker->moveToThread(m_thread);

  // Trigger work
  connect(m_thread, &QThread::started, m_worker,
          [this]() { m_worker->summarizePapers(m_hashes); });

  // When done: quit thread, then delete objects in the main thread's event loop
  connect(m_worker, &Summarizer::allFinished, m_thread, &QThread::quit);
  connect(m_thread, &QThread::finished, this, [this]() {
    m_worker->deleteLater();
    m_thread->deleteLater();
    m_worker = nullptr;
    m_thread = nullptr;
  });

  // Forward signals to QML
  connect(m_worker, &Summarizer::started, this,
          &SummaryController::summaryStarted);

  connect(m_worker, &Summarizer::allFinished, this,
          &SummaryController::summaryFinished);

  connect(m_worker, &Summarizer::paperFinished, this,
          &SummaryController::paperFinished);

  connect(m_worker, &Summarizer::allFinished, this,
          &SummaryController::allFinished);

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
