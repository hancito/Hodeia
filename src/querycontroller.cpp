#include "querycontroller.h"
#include "database.h"

#include <QDebug>

QueryController::QueryController(ConfigManager *config,
                                 DatabaseManager *database, QObject *parent)
    : QObject(parent), m_database(database), m_config(config) {}

QueryController::~QueryController() { stopQuery(); }

void QueryController::startQuery(const QString &queryText, int maxPages) {
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
  m_worker = new QueryWorker(m_config, m_database);
  m_worker->moveToThread(m_thread);

  // Capture the specific thread/worker for this run. The finished handler is
  // queued on the main thread, so if a new run starts before it fires, the
  // captured pointers let us delete *this* run's objects and avoid clobbering
  // the new run's members.
  QThread *const thread = m_thread;
  QueryWorker *const worker = m_worker;

  // Trigger work
  connect(thread, &QThread::started, worker, [worker, queryText, maxPages]() {
    worker->executeQuery(queryText, maxPages);
  });

  // When done: quit the thread.
  connect(worker, &QueryWorker::queryFinished, thread, &QThread::quit);
  connect(worker, &QueryWorker::stopped, thread, &QThread::quit);

  // Cleanup runs in the main thread's event loop after the worker thread has
  // fully finished (no pending events, worker idle). Capture thread/worker by
  // value so a stale handler from a previous run cannot delete the new run's
  // objects.
  connect(thread, &QThread::finished, this, [this, thread, worker]() {
    worker->deleteLater();
    thread->deleteLater();
    if (m_thread == thread)
      m_thread = nullptr;
    if (m_worker == worker)
      m_worker = nullptr;
  });

  // Forward signals to QML
  connect(worker, &QueryWorker::started, this, &QueryController::queryStarted);

  connect(worker, &QueryWorker::queryFinished, this,
          &QueryController::queryFinished);

  connect(worker, &QueryWorker::stopped, this, &QueryController::queryStopped);

  qInfo() << "[QueryController] Starting worker thread";
  thread->start();
}

void QueryController::stopQuery() {
  if (!m_worker || !m_thread)
    return;

  QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);

  if (m_thread->isRunning())
    m_thread->wait(3000);
}
