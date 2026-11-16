#include "enginemanager.h"
#include "ingestionengine.h"
#include <QDebug>
#include <QDirIterator>

EngineController::EngineController(ConfigManager *config,
                                   SqliteVecManager *vecManager,
                                   QObject *parent)
    : QObject(parent), m_config(config), m_vecManager(vecManager) {}

EngineController::~EngineController() {
  // Invalidate m_vecManager before stopEngine() so that any queued
  // releaseAndReloadModel() (from the worker's finished signal) becomes
  // a no-op. databaseManager is destroyed after us on the stack, so the
  // raw pointer would otherwise dangle once we're gone.
  m_vecManager = nullptr;
  stopEngine();
}

QList<QUrl> EngineController::findPdfFiles(const QUrl &folderPath) const {
  QString localPath = folderPath.toLocalFile();
  QList<QUrl> pdfs;
  QDirIterator it(localPath, {QStringLiteral("*.pdf"), QStringLiteral("*.PDF")},
                  QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    pdfs << QUrl::fromLocalFile(it.filePath());
  }
  return pdfs;
}

void EngineController::startEngine(const QList<QUrl> &pdfFiles,
                                   const QString &imgPath,
                                   const QString &dbPath) {
  // Tear down any leftover thread from a previous run.
  if (m_thread && m_thread->isRunning()) {
    return;
  }
  if (m_worker) {
    m_worker->deleteLater();
    m_worker = nullptr;
  }
  if (m_thread) {
    m_thread->wait(3000); // ensure the old thread is fully stopped
    m_thread->deleteLater();
    m_thread = nullptr;
  }

  // Capture parameters by value so the lambda is self-contained and
  // immune to the controller being destroyed or restarted.
  QList<QUrl> files = pdfFiles;
  QString img = imgPath;
  QString db = dbPath;

  m_thread = new QThread();
  m_worker = new PdfWorker(m_config, m_vecManager);
  m_worker->moveToThread(m_thread);

  // Capture raw pointers by value so the lambda cleans up the correct
  // objects even if m_thread / m_worker have been replaced by a newer run.
  QThread *thisThread = m_thread;
  PdfWorker *thisWorker = m_worker;

  // Trigger work — parameters captured by value, no `this` reference.
  connect(thisThread, &QThread::started, thisWorker,
          [thisWorker, files, img, db]() {
            thisWorker->processPDF(files, img, db);
          });

  // When done: quit thread, then delete objects in the main thread's event loop
  connect(thisWorker, &PdfWorker::finished, thisThread, &QThread::quit);
  connect(thisThread, &QThread::finished, this,
          [this, thisThread, thisWorker]() {
            thisWorker->deleteLater();
            thisThread->deleteLater();
            // Clear the controller's raw pointers to prevent use-after-free
            // if stopEngine() is called again (e.g. from the destructor).
            // Only if they still point at this run — a stale handler from a
            // previous run must not clobber a newer run's thread/worker.
            if (m_thread == thisThread)
              m_thread = nullptr;
            if (m_worker == thisWorker)
              m_worker = nullptr;
          });

  // Forward signals to QML (auto-disconnected when worker is deleted)
  connect(thisWorker, &PdfWorker::started, this,
          &EngineController::ingestionStarted);
  connect(thisWorker, &PdfWorker::status, this,
          &EngineController::ingestionStatus);
  connect(thisWorker, &PdfWorker::report, this,
          &EngineController::ingestionReport);

  // After ingestion: unload the embedding model to free its arena,
  // then reload it.  This runs on the main thread after the worker
  // has finished and been deleted.
  connect(thisWorker, &PdfWorker::finished, this,
          &EngineController::releaseAndReloadModel, Qt::QueuedConnection);

  qInfo() << "[EngineController] Starting worker thread";
  thisThread->start();
}

void EngineController::stopEngine() {
  if (!m_worker || !m_thread || !m_thread->isRunning())
    return;

  m_worker->stop();
  m_thread->requestInterruption();
  m_thread->quit();
  if (!m_thread->wait(5000)) {
    qWarning() << "[EngineController] Thread did not stop in 5s, forcing";
  }
  // Clear pointers after stop so the destructor's stopEngine() call is safe.
  // The lambda above also nullifies these when the thread finishes naturally.
  m_worker = nullptr;
  m_thread = nullptr;
}

void EngineController::releaseAndReloadModel() {
  if (!m_vecManager)
    return; // databaseManager already destroyed during shutdown
  m_vecManager->shutdown();
  emit ingestionFinished();
}
