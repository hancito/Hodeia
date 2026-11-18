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
#include <QFutureWatcher>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

ZoteroController::ZoteroController(ConfigManager *config,
                                   DatabaseManager *database,
                                   EngineController *engine, QObject *parent)
    : QObject(parent), m_config(config), m_database(database), m_engine(engine) {
  // Our import is over: mark what made it into the project.
  connect(m_engine, &EngineController::ingestionFinished, this, [this]() {
    if (m_importing) {
      m_importing = false;
      refresh();
    }
  });
  // Another project: its own followed collections and PDFs.
  connect(m_database, &DatabaseManager::databaseSwitched, this, [this]() {
    if (!m_snapshot.libraries.isEmpty() || m_busy)
      refresh();
  });
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
  setBusy(false);
  emit stateChanged();

  if (m_refreshAgain) {
    m_refreshAgain = false;
    refresh();
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
