#include "headers/enginemanager.h"
#include "headers/configmanager.h"
#include "headers/summary.h"
#include <QCoreApplication>
#include <QMetaObject>

EngineController::~EngineController() { stopThread(); }

void EngineController::startIngestion(const QStringList &files) {
  if (files.isEmpty())
    return;

  setupWorker();

  QString imgDir = m_config->projectImgPath(m_config->lastProject());
  QMetaObject::invokeMethod(m_worker, "processFiles", Qt::QueuedConnection,
                            Q_ARG(QStringList, files), Q_ARG(QString, imgDir));
}

void EngineController::startIngestionFolder(const QString &folder) {
  setupWorker();

  QString imgDir = m_config->projectImgPath(m_config->lastProject());
  QMetaObject::invokeMethod(m_worker, "processFolder", Qt::QueuedConnection,
                            Q_ARG(QString, folder), Q_ARG(QString, imgDir));
}

void EngineController::cancelActiveOperation() { stopThread(); }

void EngineController::setupWorker() {
  stopThread();

  m_workerThread = new QThread();

  SummaryModel *summaryModel =
      qobject_cast<SummaryModel *>(QCoreApplication::instance()
                                       ->property("summaryModel")
                                       .value<QObject *>());

  m_worker = new PdfWorker(summaryModel);
  m_worker->moveToThread(m_workerThread);

  // Forward signals
  connect(m_worker, &PdfWorker::progress, this, &EngineController::progress,
          Qt::QueuedConnection);
  connect(m_worker, &PdfWorker::status, this, &EngineController::statusReceived,
          Qt::QueuedConnection);
  connect(m_worker, &PdfWorker::status2, this,
          &EngineController::status2Received, Qt::QueuedConnection);
  connect(m_worker, &PdfWorker::finished, this,
          &EngineController::ingestionFinished, Qt::QueuedConnection);

  m_workerThread->start();
}

void EngineController::stopThread() {
  PdfWorker *worker = m_worker;
  QThread *thread = m_workerThread;

  m_worker = nullptr;
  m_workerThread = nullptr;

  if (worker && thread) {
    // 1. Set cancel immediately so the worker loop sees it and exits.
    worker->setCancelled();

    // 2. Queue a single event in the worker thread that deletes the worker
    //    (in its own thread) and then quits the thread.
    QMetaObject::invokeMethod(
        worker,
        [worker, thread]() {
          delete worker; // destroys in worker's own thread
          thread->quit();
        },
        Qt::QueuedConnection);

    // 3. Wait for the thread to finish. The lambda above runs once the
    //    worker's current slot (processFiles) returns.
    thread->wait(5000);

    // 4. If the thread is still running, force it.
    if (!thread->isFinished()) {
      thread->requestInterruption();
      thread->quit();
      thread->wait(2000);
    }

    // 5. Delete the thread directly. After wait(), the thread object itself
    //    is safe to delete from the calling thread.
    delete thread;
  }
}
