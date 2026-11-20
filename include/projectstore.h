#pragma once

#include <QList>
#include <QString>
#include <expected>
#include <functional>

class SqliteDb;

/// Opening of a project database and its schema migrations.
///
/// The schema version is stored in the database (PRAGMA user_version).  Each
/// migration brings it from N-1 to N inside one transaction: on failure the
/// database is left exactly as it was.  Migrations are never edited once
/// released; a schema change is a new migration appended to the list.
///
/// Version 0 is a database with no version: a new, empty file, or a project
/// of HodeIA 0.99rc, created before the migrations existed (migration 1
/// only creates what is missing).
namespace ProjectStore {

struct Migration {
  int version;      ///< schema version after this migration
  QString name;     ///< for the logs
  std::function<std::expected<void, QString>(SqliteDb &)> apply;
};

/// The migrations of this build, in order (version 1, 2, ...).
const QList<Migration> &migrations();

/// Schema version this build creates and reads (the last migration).
int currentVersion();

struct OpenError {
  enum class Kind {
    OpenFailed,      ///< the file could not be opened (detail = cause)
    TooNew,          ///< written by a newer HodeIA (dbVersion > supported)
    BackupFailed,    ///< the copy before migrating failed: nothing migrated
    MigrationFailed, ///< a migration failed and was rolled back
  };
  Kind kind;
  QString detail;
  int dbVersion = 0; ///< version found in the file
  int supportedVersion = 0; ///< version this build knows
};

/// Open `path` in `db` and bring it to the last version of `migrations`.
/// Before migrating a database that already has a version, a consistent
/// copy is saved next to it as backupPath(path, version).  On any error the
/// database is closed.
std::expected<void, OpenError>
open(SqliteDb &db, const QString &path,
     const QList<Migration> &migrations = ProjectStore::migrations());

/// PRAGMA user_version of an open database (-1 on error).
int schemaVersion(SqliteDb &db);

/// Where the copy made before migrating from `version` goes:
/// "<path>.bak-v<version>".
QString backupPath(const QString &path, int version);

} // namespace ProjectStore
