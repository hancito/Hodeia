#pragma once
#include "citationsmodel.h"
#include "query.h"
#include "queryproxymodel.h"
#include "sqlitedb.h"
#include "sqlitevec.h"
#include "summary.h"
#include "summaryproxymodel.h"
#include <QObject>
#include <expected>

class ConfigManager;

class DatabaseManager : public QObject {
  Q_OBJECT

  Q_PROPERTY(SummaryModel *summaryModel READ summaryModel CONSTANT)
  Q_PROPERTY(QueryModel *queryModel READ queryModel CONSTANT)
  Q_PROPERTY(CitationsModel *citationsModel READ citationsModel CONSTANT)
  Q_PROPERTY(
      SummaryProxyModel *summaryProxyModel READ summaryProxyModel CONSTANT)
  Q_PROPERTY(QueryProxyModel *queryProxyModel READ queryProxyModel CONSTANT)

public:
  explicit DatabaseManager(ConfigManager *config, QObject *parent = nullptr);
  ~DatabaseManager() override;

  SummaryModel *summaryModel() const { return m_summaryModel; }
  QueryModel *queryModel() const { return m_queryModel; }
  CitationsModel *citationsModel() const { return m_citationsModel; }
  SummaryProxyModel *summaryProxyModel() const { return m_summaryProxyModel; }
  QueryProxyModel *queryProxyModel() const { return m_queryProxyModel; }
  SqliteDb &db() { return m_db; }
  SqliteVecManager &vecManager() { return m_vecManager; }

  Q_INVOKABLE bool isOpen() const;
  Q_INVOKABLE QString databasePath() const;
  Q_INVOKABLE std::expected<void, QString>
  switchDatabase(const QString &dbPath);
  Q_INVOKABLE void reloadModels();
  Q_INVOKABLE void removeByHashes(const QStringList &hashes);
  Q_INVOKABLE void removeQueriesByIds(const QVariantList &ids);

private:
  bool initDatabase();
  void createTables();

  QString m_dbPath;
  SqliteDb m_db;
  ConfigManager *m_config = nullptr;
  SummaryModel *m_summaryModel = nullptr;
  QueryModel *m_queryModel = nullptr;
  CitationsModel *m_citationsModel = nullptr;
  SummaryProxyModel *m_summaryProxyModel = nullptr;
  QueryProxyModel *m_queryProxyModel = nullptr;
  SqliteVecManager m_vecManager;
};
