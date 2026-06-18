#include "headers/enginemanager.h"
#include <QDebug>
#include <QDirIterator>

EngineController::EngineController(QObject *parent) : QObject(parent) {}

EngineController::~EngineController() {
  // Sécurité : on s'assure que le thread est arrêté proprement avant de
  // détruire le contrôleur
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
  // On évite de lancer plusieurs threads en même temps
  if (m_thread && m_thread->isRunning()) {
    return;
  }

  // Sauvegarde des paramètres pour le lambda
  m_pdfFiles = pdfFiles;
  m_dbPath = dbPath;
  m_imgPath = imgPath;

  // 1. Instanciation (sans parent pour le worker, indispensable avant
  // moveToThread)
  m_thread = new QThread();
  m_worker = new PdfWorker();

  // 2. Déplacement du worker dans le nouveau thread
  m_worker->moveToThread(m_thread);

  // Dclenche le travail ds le thread worker
  connect(m_thread, &QThread::started, m_worker,
          [this]() { m_worker->processPDF(m_pdfFiles, m_imgPath, m_dbPath); });

  // Nettoyage automatique de la mémoire en cascade
  connect(m_worker, &PdfWorker::finished, m_thread, &QThread::quit);
  connect(m_worker, &PdfWorker::finished, m_worker, &QObject::deleteLater);
  connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

  // Reset des pointeurs une fois détruits pour éviter les pointeurs sauvages
  // (Dangling Pointers)
  connect(m_thread, &QThread::destroyed, this, [this]() {
    m_thread = nullptr;
    m_worker = nullptr;
  });

  // Redirection des signaux du Worker vers l'extérieur (IHM / Application)
  connect(m_worker, &PdfWorker::status, this, [this](QString statusLog) {
    qInfo() << "[EngineController] Notifying QML of new Status";
    emit ingestionStarted(statusLog);
  });
  connect(m_worker, &PdfWorker::finished, this, [this]() {
    qInfo()
        << "[EngineController] ingestion finished received, emitting to QML";
    emit ingestionFinished();
  });

  // 4. Lancement du thread
  qInfo() << "[EngineController] Starting worker thread";
  m_thread->start();
}

void EngineController::stopEngine() {
  if (m_worker && m_thread && m_thread->isRunning()) {
    m_worker->stop(); // Demande gentiment au worker de s'arrêter
    m_thread->quit(); // Demande à la boucle d'événements du thread de quitter
    m_thread->wait(); // Bloque le thread principal jusqu'à l'arrêt complet du
                      // background thread
  }
}
