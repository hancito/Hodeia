#pragma once
#include "headers/ingestionengine.h"
#include <QObject>
#include <QThread>
#include <QUrl>

class ConfigManager;
class SqliteVecManager;

class EngineController : public QObject {
  Q_OBJECT
public:
  explicit EngineController(ConfigManager *config, SqliteVecManager *vecManager,
                            QObject *parent = nullptr);
  ~EngineController() override;

public slots:
  void startEngine(const QList<QUrl> &pdfFiles, const QString &imgPath,
                   const QString &dbPath);
  void stopEngine();
  QList<QUrl> findPdfFiles(const QUrl &folderPath) const;

signals:
  void ingestionFinished();
  void ingestionStarted();
  void ingestionStatus(QString statusLog);

private:
  void setupWorker();
  void stopThread();

  QThread *m_thread = nullptr;
  PdfWorker *m_worker = nullptr;
  QList<QUrl> m_pdfFiles;
  QString m_dbPath;
  QString m_imgPath;
  ConfigManager *m_config = nullptr;
  SqliteVecManager *m_vecManager = nullptr;
};
