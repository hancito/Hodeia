#include "zoterocontroller.h"

#include "configmanager.h"
#include "database.h"
#include "enginemanager.h"
#include "libraryrepository.h"
#include "zoterolocator.h"
#include "zoterosqlitesource.h"
#include "zoterosync.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

ZoteroController::ZoteroController(ConfigManager *config,
                                   DatabaseManager *database,
                                   EngineController *engine, QObject *parent)
    : QObject(parent), m_config(config), m_database(database), m_engine(engine) {
  connect(m_engine, &EngineController::ingestionStarted, this,
          [this]() { m_ingesting = true; });
  connect(m_engine, &EngineController::ingestionFinished, this, [this]() {
    m_ingesting = false;
    if (m_importing) {
      // Our import is over: mark what made it into the project.
      m_importing = false;
      refresh();
    } else if (m_autoPending) {
      autoRefresh();
    } else if (!m_snapshot.libraries.isEmpty()) {
      // PDFs added by hand may be proposed ones: stop proposing them.
      rebuildView();
      emit stateChanged();
    }
  });
  // Another project: its own followed collections and PDFs.
  connect(m_database, &DatabaseManager::databaseSwitched, this, [this]() {
    m_announced.clear();
    m_proposals.clear();
    m_missingCount = 0;
    emit stateChanged();
    if (followsCollections() || !m_snapshot.libraries.isEmpty() || m_busy)
      refresh();
    else
      updateWatch();
  });

  // Zotero writes in bursts (an item, its attachment, the sync): wait until
  // it has been quiet for a while.  The poll catches what the watcher misses.
  m_settle.setSingleShot(true);
  m_settle.setInterval(3000);
  m_poll.setInterval(60000);
  connect(&m_settle, &QTimer::timeout, this, &ZoteroController::autoRefresh);
  connect(&m_poll, &QTimer::timeout, this, &ZoteroController::autoRefresh);
  connect(&m_watcher, &QFileSystemWatcher::fileChanged, this,
          [this]() { m_settle.start(); });
  // The WAL comes and goes with Zotero: watch it again when it is back.
  connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
    updateWatch();
    m_settle.start();
  });

  // The project opened at startup: announce what Zotero has new for it.
  QTimer::singleShot(2000, this, [this]() {
    if (m_database->isOpen() && followsCollections())
      refresh();
  });
}

bool ZoteroController::followsCollections() const {
  return m_database->isOpen() &&
         !ZoteroRepository(m_database->db()).followedCollections().isEmpty();
}

void ZoteroController::updateWatch() {
  QStringList wanted;
  if (!m_dataDir.isEmpty() && followsCollections()) {
    wanted << m_dataDir;
    for (const char *suffix : {"", "-wal"})
      if (const QString file = m_dataDir + "/zotero.sqlite" + suffix;
          QFileInfo::exists(file))
        wanted << file;
  }
  const QStringList watched = m_watcher.files() + m_watcher.directories();
  QStringList gone;
  for (const QString &path : watched)
    if (!wanted.contains(path))
      gone << path;
  if (!gone.isEmpty())
    m_watcher.removePaths(gone);
  QStringList added;
  for (const QString &path : wanted)
    if (!watched.contains(path))
      added << path;
  if (!added.isEmpty())
    m_watcher.addPaths(added);

  if (wanted.isEmpty()) {
    m_poll.stop();
    m_settle.stop();
    m_autoPending = false;
  } else if (!m_poll.isActive()) {
    m_poll.start();
  }
}

void ZoteroController::autoRefresh() {
  if (m_busy || m_ingesting) {
    m_autoPending = true;
    return;
  }
  m_autoPending = false;
  if (m_dataDir.isEmpty() || ZoteroLocator::dbSignature(m_dataDir) == m_signature)
    return;
  qInfo() << "[Zotero] the database changed, reading it again";
  refresh();
}

void ZoteroController::setBusy(bool busy) {
  if (m_busy != busy) {
    m_busy = busy;
    emit stateChanged();
  }
}

void ZoteroController::fail(const QString &message) {
  qWarning() << "[Zotero]" << message;
  m_error = message;
  setBusy(false);
  emit stateChanged();
}

QSet<QString> ZoteroController::indexedHashes() const {
  QSet<QString> hashes;
  for (const IndexedPaper &p : LibraryRepository(m_database->db()).indexedPapers())
    hashes.insert(p.paperHash);
  return hashes;
}

void ZoteroController::refresh() {
  if (m_busy) {
    m_refreshAgain = true;
    return;
  }
  const ZoteroLocator::Prefs prefs = ZoteroLocator::readPrefs();
  m_dataDir = ZoteroLocator::findDataDir(m_config->zoteroDataDir(), prefs);
  if (m_dataDir.isEmpty()) {
    m_snapshot = {};
    m_error.clear();
    updateWatch();
    emit stateChanged(); // the window asks for the folder
    return;
  }
  if (m_config->zoteroDataDir().isEmpty())
    m_config->setZoteroDataDir(m_dataDir);
  if (!m_database->isOpen()) {
    fail(tr("No project is open."));
    return;
  }

  m_error.clear();
  setBusy(true);
  // Taken before the copy: a write during the read triggers another one.
  m_signature = ZoteroLocator::dbSignature(m_dataDir);
  ZoteroRepository repo(m_database->db());
  m_loadedDbPath = m_database->databasePath();
  auto *watcher = new QFutureWatcher<Loaded>(this);
  connect(watcher, &QFutureWatcher<Loaded>::finished, this, [this, watcher]() {
    watcher->deleteLater();
    apply(watcher->result());
  });
  watcher->setFuture(QtConcurrent::run(&ZoteroController::load, m_dataDir,
                                       prefs.baseAttachmentPath,
                                       repo.followedCollections(), repo.items()));
}

ZoteroController::Loaded
ZoteroController::load(const QString &dataDir, const QString &baseAttachmentPath,
                       const QList<FollowedCollection> &followed,
                       const QList<ZoteroItemRow> &known) {
  Loaded out;
  auto snap = ZoteroSqliteSource(dataDir, baseAttachmentPath).load();
  if (!snap) {
    out.error = snap.error();
    return out;
  }
  out.snapshot = std::move(*snap);
  // Only the followed PDFs are hashed; a known, unchanged one is not read.
  out.hashes = ZoteroSync::resolveHashes(
      ZoteroSync::followedPdfs(out.snapshot, followed), known);
  return out;
}

void ZoteroController::apply(const Loaded &loaded) {
  // The project changed while Zotero was read: start over for the new one.
  if (m_database->databasePath() != m_loadedDbPath) {
    setBusy(false);
    refresh();
    return;
  }
  if (!loaded.error.isEmpty()) {
    fail(tr("Cannot read Zotero: %1").arg(loaded.error));
    updateWatch(); // try again at the next write
    return;
  }
  m_snapshot = loaded.snapshot;

  ZoteroRepository repo(m_database->db());
  const QSet<QString> indexed = indexedHashes();
  const ZoteroSync::Plan plan = ZoteroSync::plan(
      ZoteroSync::followedPdfs(m_snapshot, repo.followedCollections()),
      repo.items(), loaded.hashes, indexed);

  QString error;
  for (const ZoteroItemRow &row : plan.store)
    if (auto r = repo.upsertItem(row); !r && error.isEmpty())
      error = r.error();
  if (auto r = repo.setStatus(plan.ingestedIds, ZoteroStatus::Ingested); !r)
    error = r.error();
  if (auto r = repo.setStatus(plan.pendingIds, ZoteroStatus::Pending); !r)
    error = r.error();
  if (auto r = repo.markRemoved(plan.removedIds, QDateTime::currentDateTimeUtc()); !r)
    error = r.error();
  if (!error.isEmpty())
    m_error = tr("Cannot update the project: %1").arg(error);

  qInfo() << "[Zotero]" << plan.store.size() << "stored," << plan.ingestedIds.size()
          << "now in the project," << plan.removedIds.size() << "removed,"
          << plan.missing.size() << "not downloaded";
  m_missingCount = int(plan.missing.size());
  rebuildView();
  updateWatch();

  // Announce the proposals not announced yet; forget those that went away.
  QSet<int> proposed;
  for (const QVariant &p : std::as_const(m_proposals))
    proposed.insert(p.toMap().value("id").toInt());
  const int fresh = int((proposed - m_announced).size());
  m_announced = proposed;
  // New or corrected Zotero metadata: the paper list names papers with them.
  if (m_config->zoteroPriority() && !plan.store.isEmpty())
    m_database->reloadModels();
  setBusy(false);
  emit stateChanged();
  if (fresh > 0)
    emit newProposals(fresh);

  if (m_refreshAgain) {
    m_refreshAgain = false;
    refresh();
  } else if (m_autoPending) {
    autoRefresh();
  }
}

void ZoteroController::rebuildView() {
  ZoteroRepository repo(m_database->db());
  const QSet<QString> indexed = indexedHashes();
  const QList<ZoteroItemRow> rows = repo.items();
  m_collections.reset(m_snapshot,
                      ZoteroSync::countByCollection(m_snapshot, rows, indexed),
                      repo.followedCollections());

  // Collection names of each PDF, for the list of proposals.
  QHash<QString, QString> collectionNames;
  for (const ZoteroCollection &c : m_snapshot.collections)
    collectionNames.insert(ZoteroSync::collectionId(c.libraryId, c.key), c.name);
  QHash<QString, QStringList> pdfCollections;
  for (const ZoteroPdf &p : m_snapshot.pdfs)
    for (const QString &key : p.collectionKeys)
      pdfCollections[ZoteroSync::pdfId(p)].append(
          collectionNames.value(ZoteroSync::collectionId(p.libraryId, key)));

  m_proposals.clear();
  for (const ZoteroItemRow &r : ZoteroSync::proposals(rows, indexed))
    m_proposals.append(QVariantMap{
        {"id", r.id},
        {"title", r.meta.title},
        {"authors", r.meta.authors.join(", ")},
        {"year", r.meta.year},
        {"journal", r.meta.journal},
        {"collections", pdfCollections.value(ZoteroSync::pdfId(r)).join(", ")}});
}

QString ZoteroController::setDataDir(const QString &pathOrUrl) {
  const QUrl url(pathOrUrl);
  const QString dir = QDir::cleanPath(
      url.isLocalFile() ? url.toLocalFile() : QDir::fromNativeSeparators(pathOrUrl));
  if (!ZoteroLocator::isDataDir(dir))
    return tr("No zotero.sqlite in %1: choose Zotero's data folder (Zotero > "
              "Settings > Advanced > Files and Folders).")
        .arg(QDir::toNativeSeparators(dir));
  m_config->setZoteroDataDir(dir);
  refresh();
  return {};
}

void ZoteroController::saveCollections() {
  if (!m_database->isOpen())
    return;
  if (auto r = ZoteroRepository(m_database->db())
                   .setFollowedCollections(m_collections.followed());
      !r) {
    fail(tr("Cannot save the collections: %1").arg(r.error()));
    return;
  }
  refresh();
}

void ZoteroController::importProposals(const QVariantList &ids) {
  QSet<int> wanted;
  for (const QVariant &v : ids)
    wanted.insert(v.toInt());

  QHash<QString, QString> paths;
  for (const ZoteroPdf &p : m_snapshot.pdfs)
    if (p.onDisk)
      paths.insert(ZoteroSync::pdfId(p), p.path);

  QList<QUrl> files;
  for (const ZoteroItemRow &r : ZoteroRepository(m_database->db()).items())
    if (wanted.contains(r.id) && paths.contains(ZoteroSync::pdfId(r)))
      files.append(QUrl::fromLocalFile(paths.value(ZoteroSync::pdfId(r))));
  if (files.isEmpty())
    return;
  if (m_ingesting) {
    m_error = tr("Another import is running: import these PDFs once it has finished.");
    emit stateChanged();
    return;
  }

  m_importing = true;
  const QString project = m_config->lastProject();
  m_engine->startEngine(files, m_config->projectImgPath(project),
                        m_config->projectDbPath(project));
}

void ZoteroController::ignoreProposals(const QVariantList &ids) {
  QList<int> list;
  for (const QVariant &v : ids)
    list.append(v.toInt());
  if (auto r = ZoteroRepository(m_database->db()).setStatus(list, ZoteroStatus::Ignored);
      !r) {
    fail(tr("Cannot update the project: %1").arg(r.error()));
    return;
  }
  rebuildView();
  emit stateChanged();
}
