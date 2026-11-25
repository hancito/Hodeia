#pragma once
#include "citationsmodel.h"
#include "query.h"
#include "queryproxymodel.h"
#include "sqlitedb.h"
#include "sqlitevec.h"
#include "summary.h"
#include "summaryproxymodel.h"
#include <QObject>
#include <QReadWriteLock>
#include <QTimer>
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
  /// @p embeddings: the E5 embedding service, owned by the caller (main),
  /// which must outlive this manager.
  DatabaseManager(ConfigManager *config, SqliteVecManager &embeddings,
                  QObject *parent = nullptr);
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

  // Begin/end a worker operation that uses the shared SQLite connection
  // (a read lock on m_connectionLock, taken and released on the same
  // thread).  Closing the connection takes the write lock without waiting:
  // it is refused while an operation is in flight, and an operation that
  // begins during a switch waits for it to end.  So a worker's statement
  // never runs on a closed handle (use-after-close).
  void beginDbOperation();
  void endDbOperation();

  // The project database an import is writing to through its own
  // connection (empty when no import runs).  That project cannot be
  // deleted or cleared meanwhile.
  void setIngestionTarget(const QString &dbPath);

signals:
  // Emitted after switchDatabase() opened a new project DB.
  void databaseSwitched();
  // Summaries were written by another process (an agent through the MCP
  // server); the models are already reloaded.
  void summariesChangedElsewhere();
  // The active project's file is about to be deleted: background work with
  // its own connection to it must close it before returning.
  void databaseClosing();

private:
  bool initDatabase();
  // Make @p projectName the active project and open its database; restores
  // the previous project on failure.  @p action changes the configuration.
  QString activateProject(
      const std::function<std::expected<void, QString>()> &action);
  /// Open a project database through ProjectStore (schema migrations);
  /// a user-facing message on failure.
  std::expected<void, QString> openProjectDb(const QString &path);
  /// switchDatabase() once m_connectionLock is held for writing.
  std::expected<void, QString> switchDatabaseLocked(const QString &dbPath);
  /// Whether an import is writing to @p dbPath.
  bool isIngestingInto(const QString &dbPath) const;
  /// Every few seconds: reload the models when another process wrote
  /// summaries (cheap when nothing changed: PRAGMA data_version).
  void checkExternalChanges();

  QString m_dbPath;
  SqliteDb m_db;
  ConfigManager *m_config = nullptr;
  SummaryModel *m_summaryModel = nullptr;
  QueryModel *m_queryModel = nullptr;
  CitationsModel *m_citationsModel = nullptr;
  SummaryProxyModel *m_summaryProxyModel = nullptr;
  QueryProxyModel *m_queryProxyModel = nullptr;
  SqliteVecManager &m_vecManager;
  OpenAlexClient *m_openAlexClient = nullptr;
  QReadWriteLock m_connectionLock;
  QString m_ingestionTarget;
  QTimer *m_externalWatch = nullptr;
  int64_t m_dataVersion = -1;  ///< at the last check
  QString m_summariesSignature; ///< at the last reload
};
