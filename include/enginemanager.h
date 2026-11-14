#pragma once
#include "ingestionengine.h"
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
  void ingestionError(QString message);

private:
  void releaseAndReloadModel();

  QThread *m_thread = nullptr;
  PdfWorker *m_worker = nullptr;
  ConfigManager *m_config = nullptr;
  SqliteVecManager *m_vecManager = nullptr;
};
