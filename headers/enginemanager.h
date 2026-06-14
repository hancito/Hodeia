#pragma once
#include "headers/ingestionengine.h"
#include <QObject>
#include <QThread>

class ConfigManager;

class EngineController : public QObject {
  Q_OBJECT
public:
  explicit EngineController(ConfigManager *config, QObject *parent = nullptr)
      : QObject(parent), m_config(config) {}
  ~EngineController() override;

  Q_INVOKABLE void startIngestion(const QStringList &files);
  Q_INVOKABLE void startIngestionFolder(const QString &folder);
  Q_INVOKABLE void ingestFiles(const QStringList &files) {
    startIngestion(files);
  }
  Q_INVOKABLE void ingestFolder(const QString &folder) {
    startIngestionFolder(folder);
  }
  Q_INVOKABLE void cancelActiveOperation();

signals:
  void progressChanged(int current, int total);
  void progress(int value);
  void statusReceived(const QString &text);
  void status2Received(const QString &text);
  void ingestionFinished();

private:
  void setupWorker();
  void stopThread();

  ConfigManager *m_config = nullptr;
  QThread *m_workerThread = nullptr;
  PdfWorker *m_worker = nullptr;
};
