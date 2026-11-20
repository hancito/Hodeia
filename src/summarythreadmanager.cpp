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

  // Capture the specific thread/worker for this run. The finished handler is
  // queued on the main thread, so if a new run starts before it fires, the
  // captured pointers let us delete *this* run's objects and avoid clobbering
  // the new run's members.
  QThread *const thread = m_thread;
  Summarizer *const worker = m_worker;

  // Trigger work. Capture hashes by value so a re-entrant startSummarize
  // cannot swap m_hashes out from under an about-to-start thread.
  connect(thread, &QThread::started, worker,
          [worker, hashes]() { worker->summarizePapers(hashes); });

  // When done: quit the thread.
  connect(worker, &Summarizer::allFinished, thread, &QThread::quit);

  // Cleanup runs in the main thread's event loop after the worker thread has
  // fully finished. Capture thread/worker by value so a stale handler from a
  // previous run cannot delete the new run's objects.
  // The worker is deleted in its own thread as it ends (connected to the
  // worker); a deleteLater() from the handler below would come after the
  // thread has stopped and never run.
  connect(thread, &QThread::finished, worker, &QObject::deleteLater);
  connect(thread, &QThread::finished, this, [this, thread, worker]() {
    thread->deleteLater();
    if (m_thread == thread)
      m_thread = nullptr;
    if (m_worker == worker)
      m_worker = nullptr;
  });

  // Forward signals to QML
  connect(worker, &Summarizer::started, this,
          &SummaryController::summaryStarted);

  connect(worker, &Summarizer::allFinished, this,
          &SummaryController::summaryFinished);

  connect(worker, &Summarizer::stopped, this,
          &SummaryController::summaryStopped);

  connect(worker, &Summarizer::paperFinished, this,
          &SummaryController::paperFinished);

  connect(worker, &Summarizer::allFinished, this,
          &SummaryController::allFinished);

  qInfo() << "[SummaryController] Starting worker thread";
  thread->start();
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
