#pragma once

#include "headers/queryworker.h"
#include <QObject>
#include <QString>
#include <QThread>

class ConfigManager;
class DatabaseManager;

/// Thread-safe controller exposed to QML for RAG queries.
/// Spawns a worker thread, runs the QueryWorker, and forwards signals.
class QueryController : public QObject {
  Q_OBJECT

public:
  explicit QueryController(ConfigManager *config, DatabaseManager *database,
                           QObject *parent = nullptr);
  ~QueryController() override;

public slots:
  void startQuery(const QString &queryText, int maxPages);
  void stopQuery();

signals:
  void queryStarted();
  void queryFinished(const QString &response, bool success,
                     const QString &error);
  void queryStopped();

private:
  QThread *m_thread = nullptr;
  QueryWorker *m_worker = nullptr;
  DatabaseManager *m_database = nullptr;
  ConfigManager *m_config = nullptr;
};
