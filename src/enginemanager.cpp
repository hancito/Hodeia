#include "headers/enginemanager.h"
#include <QDebug>
#include <QDirIterator>

EngineController::EngineController(ConfigManager *config, QObject *parent)
    : QObject(parent), m_config(config) {}

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
  m_pdfFiles = pdfFiles;
  m_dbPath = dbPath;
  m_imgPath = imgPath;

  m_thread = new QThread();
  m_worker = new PdfWorker(m_config);
  m_worker->moveToThread(m_thread);

  // Trigger work
  connect(m_thread, &QThread::started, m_worker,
          [this]() { m_worker->processPDF(m_pdfFiles, m_imgPath, m_dbPath); });

  // When done: quit thread, then delete objects in the main thread's event loop
  connect(m_worker, &PdfWorker::finished, m_thread, &QThread::quit);
  connect(m_thread, &QThread::finished, this, [this]() {
    m_worker->deleteLater();
    m_thread->deleteLater();
    m_worker = nullptr;
    m_thread = nullptr;
  });

  // Forward signals to QML
  connect(m_worker, &PdfWorker::started, this,
          &EngineController::ingestionStarted);
  connect(m_worker, &PdfWorker::status, this,
          &EngineController::ingestionStatus);
  connect(m_worker, &PdfWorker::finished, this,
          &EngineController::ingestionFinished);

  qInfo() << "[EngineController] Starting worker thread";
  m_thread->start();
}

void EngineController::stopEngine() {
  if (m_worker && m_thread && m_thread->isRunning()) {
    m_worker->stop();
    m_thread->quit();
    m_thread->wait();
  }
}
