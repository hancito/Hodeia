#pragma once
#include "headers/ingestionengine.h"
#include <QObject>
#include <QThread>
#include <QUrl>

class EngineController : public QObject {
  Q_OBJECT
public:
  explicit EngineController(QObject *parent = nullptr);
  ~EngineController() override;

public slots:
  void startEngine(const QList<QUrl> &pdfFiles, const QString &imgPath,
                   const QString &dbPath);
  void stopEngine();
  QList<QUrl> findPdfFiles(const QUrl &folderPath) const;

signals:
  void ingestionFinished();
  void ingestionStarted(QString statusLog);

private:
  void setupWorker();
  void stopThread();

  QThread *m_thread = nullptr;
  PdfWorker *m_worker = nullptr;
  QList<QUrl> m_pdfFiles;
  QString m_dbPath;
  QString m_imgPath;
};
