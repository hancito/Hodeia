#include "projectstore.h"

#include "sqlitedb.h"

#include <QDebug>
#include <QFile>
#include <Sqlite3/sqlite3.h>

namespace {

/* ── Migrations ─────────────────────────────────────────────────────
   Never edit a released migration: append a new one. */

// v1 — the schema of the projects created before versioning (HodeIA
// 0.99rc).  IF NOT EXISTS: it creates a new project and leaves an existing
// one as it is.
std::expected<void, QString> migrateV1(SqliteDb &db) {
  if (auto r = db.exec("CREATE TABLE IF NOT EXISTS Summaries ("
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
      !r)
    return std::unexpected("Summaries: " + r.error());

  if (auto r = db.exec("CREATE TABLE IF NOT EXISTS Queries ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "query TEXT NOT NULL, "
                       "response TEXT NOT NULL, "
                       "date DATETIME DEFAULT CURRENT_TIMESTAMP, "
                       "llmodel TEXT NOT NULL)");
      !r)
    return std::unexpected("Queries: " + r.error());

  if (auto r = db.exec("CREATE TABLE IF NOT EXISTS Citations ("
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
      !r)
    return std::unexpected("Citations: " + r.error());

  // Text chunks with their E5 embeddings (sqlite-vec virtual table).
  if (auto r = db.exec("CREATE VIRTUAL TABLE IF NOT EXISTS chunks USING vec0("
                       "embedding float[384], "
                       "paper_hash text, "
                       "page integer, "
                       "chunk_idx integer, "
                       "chunk_text text, "
                       "citation text, "
                       "clean_title text, "
                       "page_metadata text)");
      !r)
    return std::unexpected("chunks: " + r.error());

  return {};
}

// v2 — Zotero connector: the collections a project follows and one row per
// PDF of Zotero known to the project (proposed, imported or refused).
// Linked to the other tables by paper_hash (MD5 of the file, as at import).
std::expected<void, QString> migrateV2(SqliteDb &db) {
  if (auto r = db.exec("CREATE TABLE ZoteroCollections ("
                       "library_id INTEGER NOT NULL, "
                       "collection_key TEXT NOT NULL, "
                       "name TEXT NOT NULL, "
                       "PRIMARY KEY (library_id, collection_key))");
      !r)
    return std::unexpected("ZoteroCollections: " + r.error());

  // One PDF attachment of Zotero.  Keys, not Zotero's numeric ids: they
  // survive syncs.  The same file in two libraries gives two rows with the
  // same paper_hash.  authors and tags are JSON arrays.
  if (auto r = db.exec(
          "CREATE TABLE ZoteroItems ("
          "id INTEGER PRIMARY KEY AUTOINCREMENT, "
          "library_id INTEGER NOT NULL, "
          "item_key TEXT NOT NULL, "
          "attachment_key TEXT NOT NULL, "
          "paper_hash TEXT NOT NULL, "
          "title TEXT NOT NULL DEFAULT '', "
          "authors TEXT NOT NULL DEFAULT '[]', "
          "year TEXT NOT NULL DEFAULT '', "
          "journal TEXT NOT NULL DEFAULT '', "
          "doi TEXT NOT NULL DEFAULT '', "
          "item_type TEXT NOT NULL DEFAULT '', "
          "tags TEXT NOT NULL DEFAULT '[]', "
          "date_modified TEXT NOT NULL DEFAULT '', "
          "status TEXT NOT NULL DEFAULT 'pending' "
          "  CHECK (status IN ('pending', 'ingested', 'ignored')), "
          // Set when the PDF left Zotero or the followed collections; the
          // paper itself is never deleted from the project.
          "removed_at DATETIME, "
          "added_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP, "
          "UNIQUE (library_id, attachment_key))");
      !r)
    return std::unexpected("ZoteroItems: " + r.error());

  if (auto r = db.exec("CREATE INDEX ZoteroItems_paper_hash "
                       "ON ZoteroItems (paper_hash)");
      !r)
    return std::unexpected("ZoteroItems index: " + r.error());
  return {};
}

/// A consistent copy of the open database (WAL content included) at `dest`.
std::expected<void, QString> backupTo(SqliteDb &db, const QString &dest) {
  QFile::remove(dest);
  sqlite3 *out = nullptr;
  if (sqlite3_open_v2(dest.toUtf8().constData(), &out,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                      nullptr) != SQLITE_OK) {
    const QString err = QString::fromUtf8(sqlite3_errmsg(out));
    sqlite3_close_v2(out);
    return std::unexpected(err);
  }
  sqlite3_backup *backup =
      sqlite3_backup_init(out, "main", db.handle(), "main");
  if (!backup) {
    const QString err = QString::fromUtf8(sqlite3_errmsg(out));
    sqlite3_close_v2(out);
    QFile::remove(dest);
    return std::unexpected(err);
  }
  const int rc = sqlite3_backup_step(backup, -1);
  sqlite3_backup_finish(backup);
  const QString err = QString::fromUtf8(sqlite3_errmsg(out));
  sqlite3_close_v2(out);
  if (rc != SQLITE_DONE) {
    QFile::remove(dest);
    return std::unexpected(err);
  }
  return {};
}

/// Run one migration and set the version, in one transaction.
std::expected<void, QString> applyMigration(SqliteDb &db,
                                            const ProjectStore::Migration &m) {
  if (auto r = db.transaction(); !r)
    return std::unexpected(r.error());
  auto result = m.apply(db);
  if (result)
    result = db.exec(QString("PRAGMA user_version = %1").arg(m.version));
  if (result)
    result = db.commit();
  if (!result)
    (void)db.rollback();
  return result;
}

} // namespace

namespace ProjectStore {

const QList<Migration> &migrations() {
  static const QList<Migration> kMigrations = {
      {1, QStringLiteral("initial schema"), migrateV1},
      {2, QStringLiteral("Zotero connector"), migrateV2},
  };
  return kMigrations;
}

int currentVersion() { return migrations().constLast().version; }

int schemaVersion(SqliteDb &db) {
  SqliteStatement stmt = db.prepare("PRAGMA user_version");
  if (!stmt.isValid())
    return -1;
  auto step = stmt.step();
  if (!step || !*step)
    return -1;
  return stmt.columnInt(0);
}

QString backupPath(const QString &path, int version) {
  return path + ".bak-v" + QString::number(version);
}

std::expected<void, OpenError> open(SqliteDb &db, const QString &path,
                                    const QList<Migration> &migrations) {
  const int target = migrations.isEmpty() ? 0 : migrations.constLast().version;
  auto fail = [&db](OpenError e) {
    db.close();
    return std::unexpected(std::move(e));
  };

  if (auto r = db.open(path); !r)
    return fail({OpenError::Kind::OpenFailed, r.error(), 0, target});

  const int version = schemaVersion(db);
  if (version < 0)
    return fail({OpenError::Kind::OpenFailed,
                 QStringLiteral("cannot read the schema version"), 0, target});
  if (version > target) {
    qWarning() << "[ProjectStore]" << path << "has schema version" << version
               << "— this build supports up to" << target;
    return fail({OpenError::Kind::TooNew, QString(), version, target});
  }
  if (version == target) {
    qInfo() << "[ProjectStore] Schema version" << version << "— up to date";
    return {};
  }

  // A versioned database is about to change: keep a copy first.  Version 0
  // only gets the initial schema, which leaves existing data untouched.
  if (version > 0) {
    const QString copy = backupPath(path, version);
    if (auto r = backupTo(db, copy); !r) {
      qCritical() << "[ProjectStore] Backup to" << copy
                  << "failed:" << r.error();
      return fail({OpenError::Kind::BackupFailed, r.error(), version, target});
    }
    qInfo() << "[ProjectStore] Backup before migrating:" << copy;
  }

  for (const Migration &m : migrations) {
    if (m.version <= version)
      continue;
    if (auto r = applyMigration(db, m); !r) {
      qCritical() << "[ProjectStore] Migration" << m.version << "(" << m.name
                  << ") failed:" << r.error();
      return fail({OpenError::Kind::MigrationFailed,
                   QString("v%1 (%2): %3").arg(m.version).arg(m.name,
                                                             r.error()),
                   version, target});
    }
    qInfo() << "[ProjectStore] Migrated" << path << "to version" << m.version
            << "(" << m.name << ")";
  }
  return {};
}

} // namespace ProjectStore
