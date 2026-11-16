#include "database.h"
#include "configmanager.h"
#include "openalex.h"
#include "sqlitevec.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <expected>

DatabaseManager::DatabaseManager(ConfigManager *config, QObject *parent)
    : QObject(parent), m_config(config), m_summaryModel(new SummaryModel(this)),
      m_queryModel(new QueryModel(this)),
      m_citationsModel(new CitationsModel(this)),
      m_summaryProxyModel(new SummaryProxyModel(this)),
      m_queryProxyModel(new QueryProxyModel(this)), m_vecManager(config) {
  m_summaryProxyModel->setSourceModel(m_summaryModel);
  m_queryProxyModel->setSourceModel(m_queryModel);
  m_summaryModel->setDatabase(m_db);
  m_queryModel->setDatabase(m_db);
  m_citationsModel->setDatabase(m_db);
  initDatabase();
}

DatabaseManager::~DatabaseManager() = default;

bool DatabaseManager::initDatabase() {
  if (!m_config || m_config->lastProject().isEmpty()) {
    qCritical() << "No active project set for database initialization";
    return false;
  }

  m_dbPath = m_config->projectDbPath(m_config->lastProject());
  qDebug() << "Loading project database:" << m_dbPath;

  auto openResult = m_db.open(m_dbPath);
  if (!openResult) {
    qCritical() << "Failed to open database:" << m_dbPath << openResult.error();
    return false;
  }

  createTables();

  // Embedding model is loaded lazily on first use (see SqliteVecManager).
  (void)m_vecManager.createChunksTable(m_db);

  m_summaryModel->loadAll();
  m_queryModel->loadAll();
  m_citationsModel->loadAll();

  return true;
}

void DatabaseManager::createTables() {
  qInfo() << "[Database] createTables() — ensuring schema";

  auto r1 = m_db.exec("CREATE TABLE IF NOT EXISTS Summaries ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                      "paper_hash TEXT NOT NULL UNIQUE, "
                      "title TEXT, "
                      "authors TEXT, "
                      "pages_num INTEGER NOT NULL, "
                      "journal TEXT, "
                      "year TEXT, "
                      "topics TEXT, "
                      "llmodel TEXT, "
                      "summary_json TEXT, "
                      "date DATETIME)");
  if (!r1)
    qCritical() << "[Database] Summaries table failed:" << r1.error();

  auto r2 = m_db.exec("CREATE TABLE IF NOT EXISTS Queries ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                      "query TEXT NOT NULL, "
                      "response TEXT NOT NULL, "
                      "date DATETIME DEFAULT CURRENT_TIMESTAMP, "
                      "llmodel TEXT NOT NULL)");
  if (!r2)
    qCritical() << "[Database] Queries table failed:" << r2.error();

  auto r3 = m_db.exec("CREATE TABLE IF NOT EXISTS Citations ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                      "paper_hash TEXT NOT NULL UNIQUE, "
                      "clean_title TEXT NOT NULL, "
                      "work_id TEXT NOT NULL DEFAULT '', "
                      "cites TEXT NOT NULL DEFAULT '', "
                      "cited_by TEXT NOT NULL DEFAULT '', "
                      "cites_count INTEGER NOT NULL DEFAULT 0, "
                      "cited_by_count INTEGER NOT NULL DEFAULT 0, "
                      // OpenAlex lookup state: pending | matched | not_found.
                      // not_found rows are not searched again for a while
                      // (see OpenAlexClient::dbLoadPendingJobs).
                      "lookup_status TEXT NOT NULL DEFAULT 'pending', "
                      "lookup_at DATETIME)");
  if (!r3)
    qCritical() << "[Database] Citations table failed:" << r3.error();
  else
    qInfo() << "[Database] Citations table OK";

  // Create vec0 virtual table for text chunks with embeddings
  (void)m_vecManager.createChunksTable(m_db);
}

bool DatabaseManager::isOpen() const { return m_db.isOpen(); }

QString DatabaseManager::databasePath() const { return m_dbPath; }

std::expected<void, QString>
DatabaseManager::switchDatabase(const QString &dbPath) {
  if (dbPath.isEmpty()) {
    qWarning() << "Empty database path";
    return std::unexpected("Empty database path");
  }

  // Refuse to close the connection while a worker thread is mid-statement on
  // it — that would run the worker's in-flight prepare/step/finalize on a
  // closed handle (use-after-close). The caller should retry once the
  // current operation finishes.
  if (m_opsInFlight.loadRelaxed() > 0) {
    qWarning() << "[Database] switchDatabase refused — a worker operation "
                  "is in flight. Retry when it completes.";
    return std::unexpected(
        tr("Cannot switch project while an operation (summary, query) is in "
           "progress. Try again once it has finished."));
  }

  const QString previousPath = m_dbPath;
  m_db.close();

  auto openResult = m_db.open(dbPath);
  if (!openResult) {
    qCritical() << "Failed to open database:" << dbPath << openResult.error();
    // Don't leave the app without a database: go back to the previous one.
    if (!previousPath.isEmpty() && previousPath != dbPath &&
        m_db.open(previousPath)) {
      reloadModels();
    }
    return std::unexpected(
        tr("The project database could not be opened: %1")
            .arg(openResult.error()));
  }

  m_dbPath = dbPath;
  qDebug() << "Switched database to:" << m_dbPath;
  createTables();
  reloadModels();
  emit databaseSwitched();

  return {};
}

void DatabaseManager::reloadModels() {
  m_summaryModel->loadAll();
  m_queryModel->loadAll();
  m_citationsModel->loadAll();
}

QString DatabaseManager::deleteProjects(const QStringList &projectNames) {
  if (!m_config)
    return tr("No configuration available");

  const QString active = m_config->lastProject().trimmed();
  bool deletingActive = false;
  for (const QString &name : projectNames) {
    if (name.trimmed() == active) {
      deletingActive = true;
      break;
    }
  }

  if (deletingActive) {
    // Same guard as switchDatabase(): never close the connection under a
    // worker that is mid-statement on it.
    if (m_opsInFlight.loadRelaxed() > 0) {
      qWarning() << "[Database] deleteProjects refused — a worker operation "
                    "is in flight.";
      return tr("Cannot delete the active project while an operation is in "
                "progress.");
    }
    // Release the .db (and its -wal/-shm files) so they can be deleted.
    m_db.close();
  }

  const auto result = m_config->deleteProjects(projectNames);

  // Reopen whatever is active now: the replacement project if the active one
  // was deleted, or the same one again if its deletion failed.
  if (deletingActive || !m_db.isOpen()) {
    auto switchResult =
        switchDatabase(m_config->projectDbPath(m_config->lastProject()));
    if (!switchResult)
      return result ? switchResult.error()
                    : result.error() + "\n" + switchResult.error();
  }

  return result ? QString() : result.error();
}

QString DatabaseManager::activateProject(
    const std::function<std::expected<void, QString>()> &action) {
  if (!m_config)
    return tr("No configuration available");
  // Checked before touching the configuration, so a refused switch leaves
  // the active project unchanged.
  if (m_opsInFlight.loadRelaxed() > 0)
    return tr("Cannot switch project while an operation (summary, query) is "
              "in progress. Try again once it has finished.");

  const QString previous = m_config->lastProject();
  const auto result = action();
  if (!result)
    return result.error();

  auto switchResult =
      switchDatabase(m_config->projectDbPath(m_config->lastProject()));
  if (!switchResult) {
    m_config->setLastProject(previous); // switchDatabase reopened it
    return switchResult.error();
  }
  return {};
}

QString DatabaseManager::createProject(const QString &projectName) {
  return activateProject(
      [this, projectName] { return m_config->createProject(projectName); });
}

QString DatabaseManager::switchProject(const QString &projectName) {
  return activateProject(
      [this, projectName] { return m_config->switchToProject(projectName); });
}

QString DatabaseManager::clearActiveProject() {
  if (!m_config)
    return tr("No configuration available");
  if (m_opsInFlight.loadRelaxed() > 0)
    return tr("Cannot clear the project while an operation (summary, query) "
              "is in progress. Try again once it has finished.");

  // Release the .db (and its -wal/-shm files) so they can be deleted.
  m_db.close();
  const auto result = m_config->resetActiveProject();

  // Reopen the (now empty, or untouched on failure) project database.
  m_dbPath.clear(); // nothing to fall back to: the file was just recreated
  auto switchResult =
      switchDatabase(m_config->projectDbPath(m_config->lastProject()));
  if (!switchResult)
    return result ? switchResult.error()
                  : result.error() + "\n" + switchResult.error();
  return result ? QString() : result.error();
}

void DatabaseManager::beginDbOperation() {
  m_opsInFlight.fetchAndAddOrdered(1);
}

void DatabaseManager::endDbOperation() { m_opsInFlight.fetchAndAddOrdered(-1); }

void DatabaseManager::setOpenAlexClient(OpenAlexClient *client) {
  m_openAlexClient = client;
  // referencesSaved fires on the main thread after each citation list is
  // persisted — refresh all models so the UI picks up the new data.
  connect(client, &OpenAlexClient::referencesSaved, this,
          &DatabaseManager::reloadModels);
  // Project switch: the client (own thread, queued) flushes its queue, aborts
  // the in-flight request and rescans the new DB.
  connect(this, &DatabaseManager::databaseSwitched, client,
          &OpenAlexClient::onDatabaseSwitched);
}

QString DatabaseManager::removeByHashes(const QStringList &hashes) {
  if (hashes.isEmpty())
    return {};
  if (!m_db.isOpen())
    return tr("The project database is not open.");

  QSet<QString> failed; // papers with at least one failed delete
  QString lastError;

  SqliteStatement stmt =
      m_db.prepare("DELETE FROM Summaries WHERE paper_hash = ?");
  if (!stmt.isValid())
    return tr("The papers could not be deleted (database error).");
  for (const QString &hash : hashes) {
    (void)stmt.bind(1, hash);
    auto execResult = stmt.exec();
    if (!execResult) {
      qWarning() << "Delete by hash failed for" << hash << ":"
                 << execResult.error();
      failed.insert(hash);
      lastError = execResult.error();
    }
    stmt.resetAndClearBindings();
  }

  // Also remove associated vector chunks
  SqliteStatement chunkStmt =
      m_db.prepare("DELETE FROM chunks WHERE paper_hash = ?");
  for (const QString &hash : hashes) {
    if (!chunkStmt.isValid())
      break;
    (void)chunkStmt.bind(1, hash);
    auto chunkExecResult = chunkStmt.exec();
    if (!chunkExecResult) {
      qWarning() << "Delete chunks by hash failed for" << hash << ":"
                 << chunkExecResult.error();
      failed.insert(hash);
      lastError = chunkExecResult.error();
    }
    chunkStmt.resetAndClearBindings();
  }

  // Also remove associated citations
  SqliteStatement refStmt =
      m_db.prepare("DELETE FROM Citations WHERE paper_hash = ?");
  if (refStmt.isValid()) {
    for (const QString &hash : hashes) {
      (void)refStmt.bind(1, hash);
      (void)refStmt.exec();
      refStmt.resetAndClearBindings();
    }
  }

  if (!failed.isEmpty())
    return tr("%n paper(s) could not be fully deleted: %1", nullptr,
              int(failed.size()))
        .arg(lastError);
  return {};
}

QString DatabaseManager::removeQueriesByIds(const QVariantList &ids) {
  if (ids.isEmpty())
    return {};
  if (!m_db.isOpen())
    return tr("The project database is not open.");

  SqliteStatement stmt = m_db.prepare("DELETE FROM Queries WHERE id = ?");
  if (!stmt.isValid()) {
    qCritical() << "Failed to prepare query delete";
    return tr("The queries could not be deleted (database error).");
  }

  int failed = 0;
  QString lastError;
  for (const QVariant &id : ids) {
    (void)stmt.bind(1, id.toInt());
    auto execResult = stmt.exec();
    if (!execResult) {
      qWarning() << "Delete query failed for id" << id.toInt() << ":"
                 << execResult.error();
      ++failed;
      lastError = execResult.error();
    }
    stmt.resetAndClearBindings();
  }

  m_queryModel->loadAll();
  if (failed > 0)
    return tr("%n query(ies) could not be deleted: %1", nullptr, failed)
        .arg(lastError);
  return {};
}
