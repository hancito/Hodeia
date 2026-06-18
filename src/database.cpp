#include "headers/database.h"
#include "headers/configmanager.h"
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

DatabaseManager::DatabaseManager(ConfigManager *config, QObject *parent)
    : QObject(parent), m_config(config), m_summaryModel(new SummaryModel(this)),
      m_queryModel(new QueryModel(this)),
      m_summaryProxyModel(new SummaryFilterProxyModel(this)) {
  m_summaryProxyModel->setSourceModel(m_summaryModel);
  initDatabase();
}

DatabaseManager::~DatabaseManager() {
  QSqlDatabase::removeDatabase(ProjectDbConnectionName);
}

bool DatabaseManager::initDatabase() {
  if (!m_config || m_config->lastProject().isEmpty()) {
    qCritical() << "No active project set for database initialization";
    return false;
  }

  m_dbPath = m_config->projectDbPath(m_config->lastProject());
  qDebug() << "Loading project database:" << m_dbPath;

  auto db = QSqlDatabase::addDatabase("QSQLITE", ProjectDbConnectionName);
  db.setDatabaseName(m_dbPath);

  if (!db.open()) {
    qCritical() << "Failed to open database:" << m_dbPath
                << db.lastError().text();
    return false;
  }

  createTables();
  m_summaryModel->loadAll();
  m_queryModel->loadAll();

  return true;
}

void DatabaseManager::createTables() {
  QSqlQuery query(QSqlDatabase::database(ProjectDbConnectionName));

  query.exec("CREATE TABLE IF NOT EXISTS Summaries ("
             "id INTEGER PRIMARY KEY AUTOINCREMENT, "
             "paper_hash TEXT NOT NULL, "
             "title TEXT, "
             "authors TEXT, "
             "pages_num INTEGER NOT NULL, "
             "journal TEXT, "
             "year TEXT, "
             "topics TEXT, "
             "model TEXT, "
             "summary_json TEXT, "
             "date DATETIME)");

  query.exec("CREATE TABLE IF NOT EXISTS Queries ("
             "id INTEGER PRIMARY KEY AUTOINCREMENT, "
             "query TEXT NOT NULL, "
             "response TEXT NOT NULL, "
             "date DATETIME DEFAULT CURRENT_TIMESTAMP, "
             "model TEXT NOT NULL)");

  if (query.lastError().isValid()) {
    qCritical() << "Create tables error:" << query.lastError().text();
  }
}

bool DatabaseManager::isOpen() const {
  return QSqlDatabase::database(ProjectDbConnectionName).isOpen();
}

QString DatabaseManager::databasePath() const { return m_dbPath; }

bool DatabaseManager::switchDatabase(const QString &dbPath) {
  if (dbPath.isEmpty()) {
    qWarning() << "Empty database path";
    return false;
  }

  QSqlDatabase::database(ProjectDbConnectionName).close();
  QSqlDatabase::removeDatabase(ProjectDbConnectionName);

  auto db = QSqlDatabase::addDatabase("QSQLITE", ProjectDbConnectionName);
  db.setDatabaseName(dbPath);

  if (!db.open()) {
    qCritical() << "Failed to open database:" << dbPath
                << db.lastError().text();
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
