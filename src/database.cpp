#include "database.h"
#include "configmanager.h"
#include "notifier.h"
#include "libraryrepository.h"
#include "openalex.h"
#include "projectstore.h"
#include "sqlitevec.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <expected>

namespace {

/// The write lock on the shared connection, taken without waiting: false
/// while a worker operation holds it for reading.
class ConnectionCloseLock {
public:
  explicit ConnectionCloseLock(QReadWriteLock &lock)
      : m_lock(lock), m_locked(lock.tryLockForWrite()) {}
  ~ConnectionCloseLock() {
    if (m_locked)
      m_lock.unlock();
  }
  ConnectionCloseLock(const ConnectionCloseLock &) = delete;
  ConnectionCloseLock &operator=(const ConnectionCloseLock &) = delete;
  explicit operator bool() const { return m_locked; }

private:
  QReadWriteLock &m_lock;
  bool m_locked;
};

} // namespace

DatabaseManager::DatabaseManager(ConfigManager *config,
                                 SqliteVecManager &embeddings, QObject *parent)
    : QObject(parent), m_config(config), m_summaryModel(new SummaryModel(this)),
      m_queryModel(new QueryModel(this)),
      m_citationsModel(new CitationsModel(this)),
      m_summaryProxyModel(new SummaryProxyModel(this)),
      m_queryProxyModel(new QueryProxyModel(this)), m_vecManager(embeddings) {
  m_summaryProxyModel->setSourceModel(m_summaryModel);
  m_queryProxyModel->setSourceModel(m_queryModel);
  m_summaryModel->setDatabase(m_db);
  m_queryModel->setDatabase(m_db);
  m_citationsModel->setDatabase(m_db);
  initDatabase();

  m_externalWatch = new QTimer(this);
  m_externalWatch->setInterval(2000);
  connect(m_externalWatch, &QTimer::timeout, this,
          &DatabaseManager::checkExternalChanges);
  m_externalWatch->start();
}

void DatabaseManager::checkExternalChanges() {
  if (!m_db.isOpen())
    return;
  // Changes with every commit of another connection, including HodeIA's
  // own workers (an import): then compare the summaries only.
  const int64_t version = m_db.dataVersion();
  if (version == m_dataVersion)
    return;
  m_dataVersion = version;
  if (LibraryRepository(m_db).summariesSignature() == m_summariesSignature)
    return;
  qInfo() << "[Database] Summaries written by another process: reloading";
  reloadModels();
  emit summariesChangedElsewhere();
}

DatabaseManager::~DatabaseManager() = default;

bool DatabaseManager::initDatabase() {
  if (!m_config || m_config->lastProject().isEmpty()) {
    qCritical() << "No active project set for database initialization";
    Notifier::post("db-init", tr("No Project Database"),
                   tr("No active project could be opened. Create a project "
                      "from the Project menu."));
    return false;
  }

  m_dbPath = m_config->projectDbPath(m_config->lastProject());
  qDebug() << "Loading project database:" << m_dbPath;

  if (auto opened = openProjectDb(m_dbPath); !opened) {
    qCritical() << "Failed to open database:" << m_dbPath << opened.error();
    Notifier::post(
        "db-init", tr("Project Database Unavailable"),
        tr("The database of project \"%1\" could not be opened, so the "
           "library is empty.\n\n%2\n\n%3\n\nCheck that the file exists and is "
           "not used by another program, or switch to another project.")
            .arg(m_config->lastProject(), m_dbPath, opened.error()));
    return false;
  }

  // Embedding model is loaded lazily on first use (see SqliteVecManager).
  m_summariesSignature = LibraryRepository(m_db).summariesSignature();
  m_summaryModel->loadAll();
  m_queryModel->loadAll();
  m_citationsModel->loadAll();

  return true;
}

std::expected<void, QString>
DatabaseManager::openProjectDb(const QString &path) {
  auto opened = ProjectStore::open(m_db, path);
  if (opened)
    return {};
  const ProjectStore::OpenError &e = opened.error();
  switch (e.kind) {
  case ProjectStore::OpenError::Kind::OpenFailed:
    return std::unexpected(e.detail);
  case ProjectStore::OpenError::Kind::TooNew:
    return std::unexpected(
        tr("This project was saved by a newer version of HodeIA (database "
           "version %1; this version reads up to %2). Update HodeIA to open "
           "it.")
            .arg(e.dbVersion)
            .arg(e.supportedVersion));
  case ProjectStore::OpenError::Kind::BackupFailed:
    return std::unexpected(
        tr("The project database needs an update, but the safety copy made "
           "before updating failed, so it was left unchanged: %1")
            .arg(e.detail));
  case ProjectStore::OpenError::Kind::MigrationFailed:
    break;
  }
  QString msg = tr("The project database could not be updated to the new "
                   "version; the failed step was undone.");
  // ProjectStore saves a copy before migrating a versioned database.
  if (e.dbVersion > 0)
    msg += " " + tr("A copy from before the update is kept: %1")
                     .arg(ProjectStore::backupPath(path, e.dbVersion));
  return std::unexpected(msg + "\n\n" + e.detail);
}

bool DatabaseManager::isOpen() const { return m_db.isOpen(); }

QString DatabaseManager::databasePath() const { return m_dbPath; }

std::expected<void, QString>
DatabaseManager::switchDatabase(const QString &dbPath) {
  // Refuse to close the connection while a worker thread is mid-statement on
  // it — that would run the worker's in-flight prepare/step/finalize on a
  // closed handle (use-after-close). The caller should retry once the
  // current operation finishes.
  const ConnectionCloseLock lock(m_connectionLock);
  if (!lock) {
    qWarning() << "[Database] switchDatabase refused — a worker operation "
                  "is in flight. Retry when it completes.";
    return std::unexpected(
        tr("Cannot switch project while an operation (summary, query) is in "
           "progress. Try again once it has finished."));
  }
  return switchDatabaseLocked(dbPath);
}

std::expected<void, QString>
DatabaseManager::switchDatabaseLocked(const QString &dbPath) {
  if (dbPath.isEmpty()) {
    qWarning() << "Empty database path";
    return std::unexpected("Empty database path");
  }

  const QString previousPath = m_dbPath;
  m_db.close();

  if (auto opened = openProjectDb(dbPath); !opened) {
    qCritical() << "Failed to open database:" << dbPath << opened.error();
    // Don't leave the app without a database: go back to the previous one.
    if (!previousPath.isEmpty() && previousPath != dbPath &&
        openProjectDb(previousPath)) {
      reloadModels();
    }
    return std::unexpected(
        tr("The project database could not be opened: %1")
            .arg(opened.error()));
  }

  m_dbPath = dbPath;
  qDebug() << "Switched database to:" << m_dbPath;
  reloadModels();
  emit databaseSwitched();

  return {};
}

void DatabaseManager::reloadModels() {
  m_summariesSignature = LibraryRepository(m_db).summariesSignature();
  if (m_config)
    m_summaryModel->setZoteroPriority(m_config->zoteroPriority());
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
    // An import writes to its project through its own connection: deleting
    // the files under it would lose its papers or leave a stray database.
    if (isIngestingInto(m_config->projectDbPath(name.trimmed()))) {
      qWarning() << "[Database] deleteProjects refused — an import is "
                    "writing to" << name;
      return tr("Cannot delete the project \"%1\" while papers are being "
                "imported into it.")
          .arg(name.trimmed());
    }
    if (name.trimmed() == active)
      deletingActive = true;
  }

  // Same guard as switchDatabase(): never close the connection under a
  // worker that is mid-statement on it.  Held until the new active project
  // is open.
  const ConnectionCloseLock lock(m_connectionLock);
  if (deletingActive || !m_db.isOpen()) {
    if (!lock) {
      qWarning() << "[Database] deleteProjects refused — a worker operation "
                    "is in flight.";
      return tr("Cannot delete the active project while an operation is in "
                "progress.");
    }
  }

  if (deletingActive) {
    // Release the .db (and its -wal/-shm files) so they can be deleted,
    // including the connections of background work (direct connections:
    // they are closed when this returns).
    emit databaseClosing();
    m_db.close();
  }

  const auto result = m_config->deleteProjects(projectNames);

  // Reopen whatever is active now: the replacement project if the active one
  // was deleted, or the same one again if its deletion failed.
  if (deletingActive || !m_db.isOpen()) {
    auto switchResult =
        switchDatabaseLocked(m_config->projectDbPath(m_config->lastProject()));
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
  const ConnectionCloseLock lock(m_connectionLock);
  if (!lock)
    return tr("Cannot switch project while an operation (summary, query) is "
              "in progress. Try again once it has finished.");

  const QString previous = m_config->lastProject();
  const auto result = action();
  if (!result)
    return result.error();

  auto switchResult =
      switchDatabaseLocked(m_config->projectDbPath(m_config->lastProject()));
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
  if (isIngestingInto(m_config->projectDbPath(m_config->lastProject())))
    return tr("Cannot clear the project while papers are being imported "
              "into it.");
  const ConnectionCloseLock lock(m_connectionLock);
  if (!lock)
    return tr("Cannot clear the project while an operation (summary, query) "
              "is in progress. Try again once it has finished.");

  // Release the .db (and its -wal/-shm files) so they can be deleted,
  // including the connections of background work (direct connections:
  // they are closed when this returns).
  emit databaseClosing();
  m_db.close();
  const auto result = m_config->resetActiveProject();

  // Reopen the (now empty, or untouched on failure) project database.
  m_dbPath.clear(); // nothing to fall back to: the file was just recreated
  auto switchResult =
      switchDatabaseLocked(m_config->projectDbPath(m_config->lastProject()));
  if (!switchResult)
    return result ? switchResult.error()
                  : result.error() + "\n" + switchResult.error();
  return result ? QString() : result.error();
}

void DatabaseManager::beginDbOperation() { m_connectionLock.lockForRead(); }

void DatabaseManager::endDbOperation() { m_connectionLock.unlock(); }

void DatabaseManager::setIngestionTarget(const QString &dbPath) {
  m_ingestionTarget = dbPath;
}

bool DatabaseManager::isIngestingInto(const QString &dbPath) const {
  return !m_ingestionTarget.isEmpty() &&
         QDir::cleanPath(m_ingestionTarget) == QDir::cleanPath(dbPath);
}

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

  const BatchResult r = LibraryRepository(m_db).removePapers(hashes);
  if (r.prepareFailed)
    return tr("The papers could not be deleted (database error).");
  if (r.failed > 0)
    return tr("%n paper(s) could not be fully deleted: %1", nullptr, r.failed)
        .arg(r.lastError);
  return {};
}

QString DatabaseManager::removeQueriesByIds(const QVariantList &ids) {
  if (ids.isEmpty())
    return {};
  if (!m_db.isOpen())
    return tr("The project database is not open.");

  QList<int> intIds;
  intIds.reserve(ids.size());
  for (const QVariant &id : ids)
    intIds.append(id.toInt());
  const BatchResult r = LibraryRepository(m_db).removeQueries(intIds);
  if (r.prepareFailed)
    return tr("The queries could not be deleted (database error).");

  m_queryModel->loadAll();
  if (r.failed > 0)
    return tr("%n query(ies) could not be deleted: %1", nullptr, r.failed)
        .arg(r.lastError);
  return {};
}
