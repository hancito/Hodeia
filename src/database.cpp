#include "database.h"
#include "configmanager.h"
#include "openalex.h"
#include "sqlitevec.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
        "Cannot switch project while an operation is in progress.");
  }

  m_db.close();

  auto openResult = m_db.open(dbPath);
  if (!openResult) {
    qCritical() << "Failed to open database:" << dbPath << openResult.error();
    return std::unexpected(openResult.error());
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

void DatabaseManager::removeByHashes(const QStringList &hashes) {
  if (hashes.isEmpty())
    return;

  SqliteStatement stmt =
      m_db.prepare("DELETE FROM Summaries WHERE paper_hash = ?");
  for (const QString &hash : hashes) {
    (void)stmt.bind(1, hash);
    auto execResult = stmt.exec();
    if (!execResult) {
      qDebug() << "Delete by hash failed for" << hash << ":"
               << execResult.error();
    }
    stmt.resetAndClearBindings();
  }

  // Also remove associated vector chunks
  SqliteStatement chunkStmt =
      m_db.prepare("DELETE FROM chunks WHERE paper_hash = ?");
  for (const QString &hash : hashes) {
    (void)chunkStmt.bind(1, hash);
    auto chunkExecResult = chunkStmt.exec();
    if (!chunkExecResult) {
      qDebug() << "Delete chunks by hash failed for" << hash << ":"
               << chunkExecResult.error();
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
}

void DatabaseManager::removeQueriesByIds(const QVariantList &ids) {
  if (ids.isEmpty())
    return;

  SqliteStatement stmt = m_db.prepare("DELETE FROM Queries WHERE id = ?");
  if (!stmt.isValid()) {
    qCritical() << "Failed to prepare query delete";
    return;
  }

  for (const QVariant &id : ids) {
    (void)stmt.bind(1, id.toInt());
    auto execResult = stmt.exec();
    if (!execResult) {
      qDebug() << "Delete query failed for id" << id.toInt() << ":"
               << execResult.error();
    }
    stmt.resetAndClearBindings();
  }

  m_queryModel->loadAll();
}
