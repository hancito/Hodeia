#pragma once
#include "headers/query.h"
#include "headers/summary.h"
#include <QObject>

class ConfigManager;

class DatabaseManager : public QObject {
  Q_OBJECT

  Q_PROPERTY(SummaryModel *summaryModel READ summaryModel CONSTANT)
  Q_PROPERTY(QueryModel *queryModel READ queryModel CONSTANT)

public:
  explicit DatabaseManager(ConfigManager *config, QObject *parent = nullptr);
  ~DatabaseManager() override;

  SummaryModel *summaryModel() const { return m_summaryModel; }
  QueryModel *queryModel() const { return m_queryModel; }

  Q_INVOKABLE bool isOpen() const;
  Q_INVOKABLE QString databasePath() const;
  Q_INVOKABLE bool switchDatabase(const QString &dbPath);
  Q_INVOKABLE void reloadModels();

private:
  bool initDatabase();
  void createTables();

  QString m_dbPath;
  ConfigManager *m_config = nullptr;
  SummaryModel *m_summaryModel = nullptr;
  QueryModel *m_queryModel = nullptr;
};
