// Unit tests for ProjectStore: opening project databases and migrating
// their schema (new file, unversioned project, backup, rollback, newer
// database).

#include "projectstore.h"
#include "sqlitedb.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {

bool hasTable(SqliteDb &db, const QString &name) {
  SqliteStatement stmt =
      db.prepare("SELECT 1 FROM sqlite_master WHERE name = ?");
  (void)stmt.bind(1, name);
  auto step = stmt.step();
  return step && *step;
}

int countRows(SqliteDb &db, const QString &table) {
  SqliteStatement stmt = db.prepare("SELECT COUNT(*) FROM " + table);
  auto step = stmt.step();
  return step && *step ? stmt.columnInt(0) : -1;
}

void addQuery(SqliteDb &db) {
  QVERIFY(db.exec("INSERT INTO Queries (query, response, llmodel) "
                  "VALUES ('q', 'r', 'm')"));
}

// The version after this build's, used by the tests: one more table.
int nextVersion() { return ProjectStore::currentVersion() + 1; }

ProjectStore::Migration addTable(const QString &table) {
  return {nextVersion(), "test table", [table](SqliteDb &db) {
            return db.exec("CREATE TABLE " + table + " (id INTEGER)");
          }};
}

} // namespace

class TestProjectStore : public QObject {
  Q_OBJECT

private slots:
  void newDatabase();
  void unversionedProject();
  void upToDate();
  void migrationKeepsBackup();
  void failedMigrationRollsBack();
  void tooNew();
  void openFailed();
  void v1ToV2();
};

void TestProjectStore::newDatabase() {
  QTemporaryDir dir;
  const QString path = dir.filePath("New.db");
  SqliteDb db;
  QVERIFY(ProjectStore::open(db, path).has_value());
  QVERIFY(db.isOpen());
  QCOMPARE(ProjectStore::schemaVersion(db), ProjectStore::currentVersion());
  for (const char *t : {"Summaries", "Queries", "Citations", "chunks"})
    QVERIFY2(hasTable(db, t), t);
  QVERIFY(!QFile::exists(ProjectStore::backupPath(path, 0)));
}

// A project from before the migrations: schema of v1, user_version 0,
// data.  Brought to the current version, data intact, no backup (v1 only
// creates what is missing).
void TestProjectStore::unversionedProject() {
  QTemporaryDir dir;
  const QString path = dir.filePath("Old.db");
  {
    SqliteDb db;
    QVERIFY(ProjectStore::open(db, path, ProjectStore::migrations().mid(0, 1))
                .has_value());
    addQuery(db);
    QVERIFY(db.exec("PRAGMA user_version = 0"));
  }
  SqliteDb db;
  QVERIFY(ProjectStore::open(db, path).has_value());
  QCOMPARE(ProjectStore::schemaVersion(db), ProjectStore::currentVersion());
  QCOMPARE(countRows(db, "Queries"), 1);
  QVERIFY(!QFile::exists(ProjectStore::backupPath(path, 0)));
}

void TestProjectStore::upToDate() {
  QTemporaryDir dir;
  const QString path = dir.filePath("P.db");
  {
    SqliteDb db;
    QVERIFY(ProjectStore::open(db, path).has_value());
  }
  SqliteDb db;
  QVERIFY(ProjectStore::open(db, path).has_value());
  QCOMPARE(ProjectStore::schemaVersion(db), ProjectStore::currentVersion());
  QVERIFY(!QFile::exists(
      ProjectStore::backupPath(path, ProjectStore::currentVersion())));
}

// Migrating a versioned database saves a copy of it first.
void TestProjectStore::migrationKeepsBackup() {
  QTemporaryDir dir;
  const QString path = dir.filePath("P.db");
  {
    SqliteDb db;
    QVERIFY(ProjectStore::open(db, path).has_value());
    addQuery(db); // still in the WAL: the copy must include it
  }
  QList<ProjectStore::Migration> migrations = ProjectStore::migrations();
  migrations.append(addTable("Extra"));

  SqliteDb db;
  QVERIFY(ProjectStore::open(db, path, migrations).has_value());
  QCOMPARE(ProjectStore::schemaVersion(db), nextVersion());
  QVERIFY(hasTable(db, "Extra"));
  QCOMPARE(countRows(db, "Queries"), 1);

  const QString copy =
      ProjectStore::backupPath(path, ProjectStore::currentVersion());
  QVERIFY(QFile::exists(copy));
  SqliteDb old;
  QVERIFY(old.open(copy).has_value());
  QCOMPARE(ProjectStore::schemaVersion(old), ProjectStore::currentVersion());
  QVERIFY(!hasTable(old, "Extra"));
  QCOMPARE(countRows(old, "Queries"), 1);
}

// A failing migration leaves the database as it was before it.
void TestProjectStore::failedMigrationRollsBack() {
  QTemporaryDir dir;
  const QString path = dir.filePath("P.db");
  {
    SqliteDb db;
    QVERIFY(ProjectStore::open(db, path).has_value());
    addQuery(db);
  }
  QList<ProjectStore::Migration> migrations = ProjectStore::migrations();
  migrations.append({nextVersion(), "broken", [](SqliteDb &db) {
                       (void)db.exec("CREATE TABLE Half (id INTEGER)");
                       (void)db.exec("DELETE FROM Queries");
                       return db.exec("THIS IS NOT SQL");
                     }});

  SqliteDb db;
  auto r = ProjectStore::open(db, path, migrations);
  QVERIFY(!r.has_value());
  QCOMPARE(r.error().kind, ProjectStore::OpenError::Kind::MigrationFailed);
  QVERIFY(r.error().detail.contains("broken"));
  QVERIFY(!db.isOpen());

  SqliteDb check;
  QVERIFY(check.open(path).has_value());
  QCOMPARE(ProjectStore::schemaVersion(check), ProjectStore::currentVersion());
  QVERIFY(!hasTable(check, "Half"));
  QCOMPARE(countRows(check, "Queries"), 1);
}

// A database from a newer HodeIA is refused and left untouched.
void TestProjectStore::tooNew() {
  QTemporaryDir dir;
  const QString path = dir.filePath("P.db");
  {
    SqliteDb db;
    QVERIFY(ProjectStore::open(db, path).has_value());
    QVERIFY(db.exec("PRAGMA user_version = 99"));
  }
  SqliteDb db;
  auto r = ProjectStore::open(db, path);
  QVERIFY(!r.has_value());
  QCOMPARE(r.error().kind, ProjectStore::OpenError::Kind::TooNew);
  QCOMPARE(r.error().dbVersion, 99);
  QCOMPARE(r.error().supportedVersion, ProjectStore::currentVersion());
  QVERIFY(!db.isOpen());

  SqliteDb check;
  QVERIFY(check.open(path).has_value());
  QCOMPARE(ProjectStore::schemaVersion(check), 99);
}

void TestProjectStore::openFailed() {
  SqliteDb db;
  auto r = ProjectStore::open(db, "/nonexistent-dir/for-sure/P.db");
  QVERIFY(!r.has_value());
  QCOMPARE(r.error().kind, ProjectStore::OpenError::Kind::OpenFailed);
  QVERIFY(!db.isOpen());
}

// A project of HodeIA 0.99rc (version 1) gains the Zotero tables; its data
// and a v1 copy are kept.
void TestProjectStore::v1ToV2() {
  QTemporaryDir dir;
  const QString path = dir.filePath("P.db");
  {
    SqliteDb db;
    QVERIFY(ProjectStore::open(db, path, ProjectStore::migrations().mid(0, 1))
                .has_value());
    addQuery(db);
    QCOMPARE(ProjectStore::schemaVersion(db), 1);
  }
  SqliteDb db;
  QVERIFY(ProjectStore::open(db, path).has_value());
  QVERIFY(ProjectStore::schemaVersion(db) >= 2);
  QVERIFY(hasTable(db, "ZoteroItems"));
  QVERIFY(hasTable(db, "ZoteroCollections"));
  QCOMPARE(countRows(db, "Queries"), 1);
  QVERIFY(QFile::exists(ProjectStore::backupPath(path, 1)));

  // A status outside the three known ones is refused.
  QVERIFY(db.exec("INSERT INTO ZoteroItems (library_id, item_key, "
                  "attachment_key, paper_hash) VALUES (1, 'I', 'A', 'h')"));
  QVERIFY(!db.exec("UPDATE ZoteroItems SET status = 'lost'"));
  // One row per attachment of a library.
  QVERIFY(!db.exec("INSERT INTO ZoteroItems (library_id, item_key, "
                   "attachment_key, paper_hash) VALUES (1, 'I', 'A', 'h')"));
}

QTEST_GUILESS_MAIN(TestProjectStore)
#include "tst_projectstore.moc"
