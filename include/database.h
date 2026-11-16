#pragma once
#include "citationsmodel.h"
#include "query.h"
#include "queryproxymodel.h"
#include "sqlitedb.h"
#include "sqlitevec.h"
#include "summary.h"
#include "summaryproxymodel.h"
#include <QAtomicInt>
#include <QObject>
#include <expected>
#include <functional>

class ConfigManager;
class OpenAlexClient;

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
  // Open another database.  On failure the previous one is reopened.
  std::expected<void, QString> switchDatabase(const QString &dbPath);
  Q_INVOKABLE void reloadModels();
  // Project operations for QML: update the configuration and (re)open the
  // project database.  Each returns an empty string on success, otherwise a
  // user-facing error message; on failure the previous project stays active.
  Q_INVOKABLE QString createProject(const QString &projectName);
  Q_INVOKABLE QString switchProject(const QString &projectName);
  // Delete every paper, summary and query of the active project.
  Q_INVOKABLE QString clearActiveProject();
  // Delete projects from disk.  Closes the shared connection first when the
  // active project is among them (an open SQLite file cannot be deleted on
  // Windows), then opens the new active project's database.  Returns an
  // empty string on success, otherwise a user-facing error message.
  Q_INVOKABLE QString deleteProjects(const QStringList &projectNames);
  // Wire up the OpenAlex client so a finished citation save refreshes all
  // models.  Called once from main() after the client is constructed.
  void setOpenAlexClient(OpenAlexClient *client);
  // Delete papers (summaries, chunks, citations) / queries.  Return an empty
  // string on success, otherwise a user-facing error message.
  Q_INVOKABLE QString removeByHashes(const QStringList &hashes);
  Q_INVOKABLE QString removeQueriesByIds(const QVariantList &ids);

  // Begin/end a worker operation that uses the shared SQLite connection.
  // switchDatabase() refuses to close the connection while any operation is
  // in flight, so a worker's in-flight statement cannot run on a closed
  // handle (use-after-close).
  void beginDbOperation();
  void endDbOperation();

signals:
  // Emitted after switchDatabase() opened a new project DB.
  void databaseSwitched();

private:
  bool initDatabase();
  // Make @p projectName the active project and open its database; restores
  // the previous project on failure.  @p action changes the configuration.
  QString activateProject(
      const std::function<std::expected<void, QString>()> &action);
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
  OpenAlexClient *m_openAlexClient = nullptr;
  QAtomicInt m_opsInFlight{0};
};
