#include "querycontroller.h"
#include "database.h"

#include <QDebug>

QueryController::QueryController(ConfigManager *config,
                                 DatabaseManager *database, QObject *parent)
    : QObject(parent), m_config(config), m_database(database) {}

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

  // Trigger work
  connect(m_thread, &QThread::started, m_worker, [this, queryText, maxPages]() {
    m_worker->executeQuery(queryText, maxPages);
  });

  // When done: quit thread, then delete objects in the main thread's event loop
  connect(m_worker, &QueryWorker::queryFinished, m_thread, &QThread::quit);
  connect(m_worker, &QueryWorker::stopped, m_thread, &QThread::quit);
  connect(m_thread, &QThread::finished, this, [this]() {
    m_worker->deleteLater();
    m_thread->deleteLater();
    m_worker = nullptr;
    m_thread = nullptr;
  });

  // Forward signals to QML
  connect(m_worker, &QueryWorker::started, this,
          &QueryController::queryStarted);

  connect(m_worker, &QueryWorker::queryFinished, this,
          &QueryController::queryFinished);

  connect(m_worker, &QueryWorker::stopped, this,
          &QueryController::queryStopped);

  // When the worker signals stopped, ask its thread to quit.
  connect(m_worker, &QueryWorker::stopped, m_thread, &QThread::quit,
          Qt::QueuedConnection);

  qInfo() << "[QueryController] Starting worker thread";
  m_thread->start();
}

void QueryController::stopQuery() {
  if (!m_worker || !m_thread)
    return;

  QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);

  if (m_thread->isRunning())
    m_thread->wait(3000);
}
