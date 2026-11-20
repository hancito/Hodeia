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

// A project of HodeIA 0.99rc: its tables as that release created them,
// no version, one query and, if asked, one chunk.
void makeRcProject(const QString &path, bool withChunk) {
  SqliteDb db;
  QVERIFY(db.open(path).has_value());
  QVERIFY(db.exec("CREATE TABLE Summaries (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "paper_hash TEXT NOT NULL UNIQUE, title TEXT, authors TEXT, "
                  "pages_num INTEGER NOT NULL, journal TEXT, year TEXT, topics TEXT, "
                  "llmodel TEXT, summary_json TEXT, date DATETIME)"));
  QVERIFY(db.exec("CREATE TABLE Queries (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "query TEXT NOT NULL, response TEXT NOT NULL, "
                  "date DATETIME DEFAULT CURRENT_TIMESTAMP, llmodel TEXT NOT NULL)"));
  QVERIFY(db.exec("CREATE TABLE Citations (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "paper_hash TEXT NOT NULL UNIQUE, clean_title TEXT NOT NULL, "
                  "work_id TEXT NOT NULL DEFAULT '', cites TEXT NOT NULL DEFAULT '', "
                  "cited_by TEXT NOT NULL DEFAULT '', "
                  "cites_count INTEGER NOT NULL DEFAULT 0, "
                  "cited_by_count INTEGER NOT NULL DEFAULT 0, "
                  "lookup_status TEXT NOT NULL DEFAULT 'pending', lookup_at DATETIME)"));
  QVERIFY(db.exec("CREATE VIRTUAL TABLE chunks USING vec0(embedding float[384], "
                  "paper_hash text, page integer, chunk_idx integer, chunk_text text, "
                  "citation text, clean_title text, page_metadata text)"));
  addQuery(db);
  if (withChunk) {
    // vec0 metadata columns take no NULL: every column is given.
    SqliteStatement insert = db.prepare(
        "INSERT INTO chunks (embedding, paper_hash, page, chunk_idx, "
        "chunk_text, citation, clean_title, page_metadata) "
        "VALUES (vec_f32(?), 'h', 1, 0, 'text', '', '', '')");
    (void)insert.bind(1, QByteArray(384 * sizeof(float), '\0'));
    const auto r = insert.exec();
    QVERIFY2(r.has_value(), r ? "" : qPrintable(r.error()));
  }
  QCOMPARE(ProjectStore::schemaVersion(db), 0);
}

const char *const kTables[] = {"Summaries",          "Queries",     "Citations",
                               "chunks",             "ZoteroItems", "ZoteroCollections",
                               "Maintenance",        "Conversations",
                               "ConversationMessages"};

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
  void zoteroItemsRules();
};

void TestProjectStore::newDatabase() {
  QTemporaryDir dir;
  const QString path = dir.filePath("New.db");
  SqliteDb db;
  QVERIFY(ProjectStore::open(db, path).has_value());
  QVERIFY(db.isOpen());
  QCOMPARE(ProjectStore::schemaVersion(db), ProjectStore::currentVersion());
  for (const char *t : kTables)
    QVERIFY2(hasTable(db, t), t);
  QCOMPARE(countRows(db, "Maintenance"), 0); // nothing to re-embed
  QVERIFY(!QFile::exists(ProjectStore::backupPath(path, 0)));
}

// A project of HodeIA 0.99rc (version 0) gains the tables added since; its
// data is intact, no backup is made (v1 only creates what is missing), and
// its chunks are queued for re-embedding (none queued without chunks).
void TestProjectStore::unversionedProject() {
  QTemporaryDir dir;
  for (const bool withChunk : {true, false}) {
    const QString path = dir.filePath(withChunk ? "Full.db" : "Empty.db");
    makeRcProject(path, withChunk);
    if (QTest::currentTestFailed())
      return;
    SqliteDb db;
    QVERIFY(ProjectStore::open(db, path).has_value());
    QCOMPARE(ProjectStore::schemaVersion(db), ProjectStore::currentVersion());
    for (const char *t : kTables)
      QVERIFY2(hasTable(db, t), t);
    QCOMPARE(countRows(db, "Queries"), 1);
    QCOMPARE(countRows(db, "chunks"), withChunk ? 1 : 0);
    QCOMPARE(countRows(db, "Maintenance WHERE task = 'reembed' AND progress = 0"),
             withChunk ? 1 : 0);
    QVERIFY(!QFile::exists(ProjectStore::backupPath(path, 0)));
  }
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

// ZoteroItems: a status outside the three known ones is refused, one row
// per attachment of a library, and the same PDF may be in two libraries.
void TestProjectStore::zoteroItemsRules() {
  QTemporaryDir dir;
  SqliteDb db;
  QVERIFY(ProjectStore::open(db, dir.filePath("P.db")).has_value());
  QVERIFY(db.exec("INSERT INTO ZoteroItems (library_id, item_key, "
                  "attachment_key, paper_hash) VALUES (1, 'I', 'A', 'h')"));
  QVERIFY(!db.exec("UPDATE ZoteroItems SET status = 'lost'"));
  QVERIFY(!db.exec("INSERT INTO ZoteroItems (library_id, item_key, "
                   "attachment_key, paper_hash) VALUES (1, 'I', 'A', 'h')"));
  QVERIFY(db.exec("INSERT INTO ZoteroItems (library_id, item_key, "
                  "attachment_key, paper_hash) VALUES (2, 'I', 'A', 'h')"));
}

QTEST_GUILESS_MAIN(TestProjectStore)
#include "tst_projectstore.moc"
