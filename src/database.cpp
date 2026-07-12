#include "headers/database.h"
#include "headers/configmanager.h"
#include "headers/sqlitevec.h"
#include <QDir>
#include <expected>

DatabaseManager::DatabaseManager(ConfigManager *config, QObject *parent)
    : QObject(parent), m_config(config), m_summaryModel(new SummaryModel(this)),
      m_queryModel(new QueryModel(this)),
      m_summaryProxyModel(new SummaryProxyModel(this)), m_vecManager(config) {
  m_summaryProxyModel->setSourceModel(m_summaryModel);
  m_summaryModel->setDatabase(m_db);
  m_queryModel->setDatabase(m_db);
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

  return true;
}

void DatabaseManager::createTables() {
  (void)m_db.exec("CREATE TABLE IF NOT EXISTS Summaries ("
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

  (void)m_db.exec("CREATE TABLE IF NOT EXISTS Queries ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "query TEXT NOT NULL, "
                  "response TEXT NOT NULL, "
                  "date DATETIME DEFAULT CURRENT_TIMESTAMP, "
                  "llmodel TEXT NOT NULL)");

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

  return {};
}

void DatabaseManager::reloadModels() {
  m_summaryModel->loadAll();
  m_queryModel->loadAll();
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
