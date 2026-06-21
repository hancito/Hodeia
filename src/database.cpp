#include "headers/database.h"
#include "headers/configmanager.h"
#include "headers/sqlitevec.h"
#include <QDir>

DatabaseManager::DatabaseManager(ConfigManager *config, QObject *parent)
    : QObject(parent), m_config(config), m_summaryModel(new SummaryModel(this)),
      m_queryModel(new QueryModel(this)),
      m_summaryProxyModel(new SummaryProxyModel(this)) {
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

  if (!m_db.open(m_dbPath)) {
    qCritical() << "Failed to open database:" << m_dbPath << m_db.lastError();
    return false;
  }

  createTables();

  // Initialize embedding model (idempotent)
  m_vecManager.initialize();

  m_summaryModel->loadAll();
  m_queryModel->loadAll();

  return true;
}

void DatabaseManager::createTables() {
  m_db.exec("CREATE TABLE IF NOT EXISTS Summaries ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "paper_hash TEXT NOT NULL, "
            "title TEXT, "
            "authors TEXT, "
            "pages_num INTEGER NOT NULL, "
            "journal TEXT, "
            "year TEXT, "
            "topics TEXT, "
            "llmodel TEXT, "
            "summary_json TEXT, "
            "date DATETIME)");

  m_db.exec("CREATE TABLE IF NOT EXISTS Queries ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "query TEXT NOT NULL, "
            "response TEXT NOT NULL, "
            "date DATETIME DEFAULT CURRENT_TIMESTAMP, "
            "llmodel TEXT NOT NULL)");

  // Create vec0 virtual table for text chunks with embeddings
  m_vecManager.createChunksTable(m_db);
}

bool DatabaseManager::isOpen() const { return m_db.isOpen(); }

QString DatabaseManager::databasePath() const { return m_dbPath; }

bool DatabaseManager::switchDatabase(const QString &dbPath) {
  if (dbPath.isEmpty()) {
    qWarning() << "Empty database path";
    return false;
  }

  m_db.close();

  if (!m_db.open(dbPath)) {
    qCritical() << "Failed to open database:" << dbPath << m_db.lastError();
    return false;
  }

  m_dbPath = dbPath;
  qDebug() << "Switched database to:" << m_dbPath;
  createTables();
  reloadModels();

  return true;
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
    stmt.bind(1, hash);
    if (!stmt.exec()) {
      qDebug() << "Delete by hash failed for" << hash << ":" << stmt.errorMsg();
    }
    stmt.resetAndClearBindings();
  }

  // Also remove associated vector chunks
  // We need to parse the metadata JSON to match paper_hash; use a LIKE filter
  for (const QString &hash : hashes) {
    QString escapedHash = hash;
    m_db.exec(QString("DELETE FROM chunks WHERE metadata_blob LIKE '%1%'")
                  .arg(escapedHash));
  }

  m_summaryModel->loadAll();
  m_queryModel->loadAll();
}
