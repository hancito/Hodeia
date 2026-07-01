#include "headers/enginemanager.h"
#include "headers/ingestionengine.h"
#include <QDebug>
#include <QDirIterator>

EngineController::EngineController(ConfigManager *config,
                                   SqliteVecManager *vecManager,
                                   QObject *parent)
    : QObject(parent), m_config(config), m_vecManager(vecManager) {}

EngineController::~EngineController() { stopEngine(); }

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
    m_thread->deleteLater();
    m_thread = nullptr;
  }

  // Save parameters
  m_pdfFiles = pdfFiles;
  m_dbPath = dbPath;
  m_imgPath = imgPath;

  m_thread = new QThread();
  m_worker = new PdfWorker(m_config, m_vecManager);
  m_worker->moveToThread(m_thread);

  // Capture raw pointers by value so the lambda cleans up the correct
  // objects even if m_thread / m_worker have been replaced by a newer run.
  QThread *thisThread = m_thread;
  PdfWorker *thisWorker = m_worker;

  // Trigger work
  connect(thisThread, &QThread::started, thisWorker, [thisWorker, this]() {
    thisWorker->processPDF(m_pdfFiles, m_imgPath, m_dbPath);
  });

  // When done: quit thread, then delete objects in the main thread's event loop
  connect(thisWorker, &PdfWorker::finished, thisThread, &QThread::quit);
  connect(thisThread, &QThread::finished, this, [thisThread, thisWorker]() {
    thisWorker->deleteLater();
    thisThread->deleteLater();
  });

  // Forward signals to QML (auto-disconnected when worker is deleted)
  connect(thisWorker, &PdfWorker::started, this,
          &EngineController::ingestionStarted);
  connect(thisWorker, &PdfWorker::status, this,
          &EngineController::ingestionStatus);

  // After ingestion: unload the embedding model to free its arena,
  // then reload it.  This runs on the main thread after the worker
  // has finished and been deleted.
  connect(thisWorker, &PdfWorker::finished, this,
          &EngineController::releaseAndReloadModel, Qt::QueuedConnection);

  qInfo() << "[EngineController] Starting worker thread";
  thisThread->start();
}

void EngineController::stopEngine() {
  if (m_worker && m_thread && m_thread->isRunning()) {
    m_worker->stop();
    m_thread->quit();
    m_thread->wait();
  }
}

void EngineController::releaseAndReloadModel() {
  // Unload the embedding model to free its arena (RAM + VRAM) that would
  // otherwise stay allocated for the lifetime of the process.
  // The model will be reloaded lazily on the next embed/search/insert call.
  m_vecManager->shutdown();
  emit ingestionFinished();
}
