#include "summarythreadmanager.h"
#include "database.h"

#include <QDebug>

SummaryController::SummaryController(ConfigManager *config,
                                     DatabaseManager *database, QObject *parent)
    : QObject(parent), m_database(database), m_config(config) {}

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

  connect(m_worker, &Summarizer::stopped, this,
          &SummaryController::summaryStopped);

  // When the worker signals stopped, ask its thread to quit.
  connect(m_worker, &Summarizer::stopped, m_thread, &QThread::quit,
          Qt::QueuedConnection);

  connect(m_worker, &Summarizer::paperFinished, this,
          &SummaryController::paperFinished);

  connect(m_worker, &Summarizer::allFinished, this,
          &SummaryController::allFinished);

  qInfo() << "[EngineController] Starting worker thread";
  m_thread->start();
}

void SummaryController::stopSummarize() {
  if (!m_worker || !m_thread)
    return;

  // Post stop() into the worker's event loop so it runs in the correct
  // thread (the same thread that owns m_chatBackend / m_remoteBackend).
  // The worker emits stopped() after cleanup, which quits the thread
  // (connected in startSummarize).
  QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);

  // Wait for the thread to finish (up to 3s).
  // The thread will quit when:
  //   - remote: cancelled request finishes → loop exits → stopped() → quit
  //   - local: current paper finishes → loop sees m_stopping → stopped() → quit
  if (m_thread->isRunning())
    m_thread->wait(3000);
}
