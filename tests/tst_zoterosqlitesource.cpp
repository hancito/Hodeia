// Unit tests for ZoteroSqliteSource: reading a Zotero library from a
// zotero.sqlite built here (only the tables and columns HodeIA reads, with
// the names of Zotero 7's schema), including changes still in the WAL.

#include "sqlitedb.h"
#include "zoterolocator.h"
#include "zoterosqlitesource.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {

void writeFile(const QString &path) {
  QDir().mkpath(QFileInfo(path).path());
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("%PDF-1.4\n");
}

const char *const kSchema[] = {
    "CREATE TABLE version (schema TEXT PRIMARY KEY, version INT NOT NULL)",
    "CREATE TABLE libraries (libraryID INTEGER PRIMARY KEY, type TEXT NOT NULL)",
    "CREATE TABLE groups (groupID INTEGER PRIMARY KEY, libraryID INT, name TEXT)",
    "CREATE TABLE collections (collectionID INTEGER PRIMARY KEY, "
    "collectionName TEXT, parentCollectionID INT, libraryID INT, key TEXT)",
    "CREATE TABLE deletedCollections (collectionID INTEGER PRIMARY KEY, dateDeleted TEXT)",
    "CREATE TABLE collectionItems (collectionID INT, itemID INT, orderIndex INT)",
    "CREATE TABLE itemTypes (itemTypeID INTEGER PRIMARY KEY, typeName TEXT)",
    "CREATE TABLE items (itemID INTEGER PRIMARY KEY, itemTypeID INT, "
    "dateModified TEXT, libraryID INT, key TEXT)",
    "CREATE TABLE deletedItems (itemID INTEGER PRIMARY KEY, dateDeleted TEXT)",
    "CREATE TABLE itemAttachments (itemID INTEGER PRIMARY KEY, parentItemID INT, "
    "linkMode INT, contentType TEXT, path TEXT, storageHash TEXT)",
    "CREATE TABLE fields (fieldID INTEGER PRIMARY KEY, fieldName TEXT)",
    "CREATE TABLE itemDataValues (valueID INTEGER PRIMARY KEY, value TEXT)",
    "CREATE TABLE itemData (itemID INT, fieldID INT, valueID INT)",
    "CREATE TABLE creatorTypes (creatorTypeID INTEGER PRIMARY KEY, creatorType TEXT)",
    "CREATE TABLE creators (creatorID INTEGER PRIMARY KEY, firstName TEXT, "
    "lastName TEXT, fieldMode INT)",
    "CREATE TABLE itemCreators (itemID INT, creatorID INT, creatorTypeID INT, orderIndex INT)",
    "CREATE TABLE tags (tagID INTEGER PRIMARY KEY, name TEXT)",
    "CREATE TABLE itemTags (itemID INT, tagID INT, type INT)",

    "INSERT INTO version VALUES ('userdata', 131), ('globalSchema', 44)",
    "INSERT INTO libraries VALUES (1, 'user'), (2, 'group'), (3, 'feed')",
    "INSERT INTO groups VALUES (99, 2, 'Lab')",
    "INSERT INTO collections VALUES (1, 'Thesis', NULL, 1, 'COLTHES1'), "
    "(2, 'Chapter 2', 1, 1, 'COLCH2AA'), (3, 'Old', NULL, 1, 'COLOLD11'), "
    "(4, 'Shared', NULL, 2, 'COLSHAR1')",
    "INSERT INTO deletedCollections VALUES (3, '2026-01-01')",
    "INSERT INTO itemTypes VALUES (1, 'journalArticle'), (2, 'book'), (3, 'attachment')",
    "INSERT INTO fields VALUES (1, 'title'), (2, 'date'), (3, 'publicationTitle'), "
    "(4, 'DOI'), (5, 'url')",
    "INSERT INTO creatorTypes VALUES (1, 'author'), (2, 'editor')",

    // A journal article with two PDFs (one not downloaded) and a web link.
    "INSERT INTO items VALUES (10, 1, '2026-01-01 10:00:00', 1, 'ITEMA111'), "
    "(11, 3, '2026-02-01 09:00:00', 1, 'ATTA1111'), "
    "(12, 3, '2025-12-01 09:00:00', 1, 'ATTA2222'), "
    "(13, 3, '2025-12-01 09:00:00', 1, 'ATTHTML1')",
    "INSERT INTO itemAttachments VALUES "
    "(11, 10, 0, 'application/pdf', 'storage:a.pdf', 'ABCDEF0123'), "
    "(12, 10, 1, 'application/pdf', 'storage:a2.pdf', NULL), "
    "(13, 10, 3, 'text/html', 'https://example.org', NULL)",
    "INSERT INTO itemDataValues VALUES (1, 'Strategies'), "
    "(2, '2014-09-01 2014-09-01'), (3, 'Games'), (4, '10.1/x'), "
    "(5, 'A Book'), (6, '0000-00-00 Spring 2019'), (7, 'Loose notes'), "
    "(8, 'Group paper'), (9, 'Trashed')",
    "INSERT INTO itemData VALUES (10, 1, 1), (10, 2, 2), (10, 3, 3), (10, 4, 4)",
    "INSERT INTO creators VALUES (1, 'Christos A.', 'Ioannou', 0), "
    "(2, 'Julian', 'Romero', 0), (3, 'Ed', 'Itor', 0), "
    "(4, 'Jane', 'Doe', 0), (5, NULL, 'World Bank', 1)",
    "INSERT INTO itemCreators VALUES (10, 3, 2, 2), (10, 2, 1, 1), (10, 1, 1, 0)",
    "INSERT INTO tags VALUES (1, 'game theory'), (2, 'Absenteeism')",
    "INSERT INTO itemTags VALUES (10, 1, 0), (10, 2, 0)",
    "INSERT INTO collectionItems VALUES (1, 10, 0), (2, 10, 0)",

    // A book with editors only, in a trashed collection; its PDF is a file
    // linked relative to the base folder.
    "INSERT INTO items VALUES (20, 2, '2026-01-01 10:00:00', 1, 'ITEMB111'), "
    "(21, 3, '2026-01-01 10:00:00', 1, 'ATTB1111')",
    "INSERT INTO itemAttachments VALUES "
    "(21, 20, 2, 'application/pdf', 'attachments:books/b.pdf', NULL)",
    "INSERT INTO itemData VALUES (20, 1, 5), (20, 2, 6)",
    "INSERT INTO itemCreators VALUES (20, 4, 2, 0), (20, 5, 2, 1)",
    "INSERT INTO collectionItems VALUES (3, 20, 0)",

    // A standalone PDF (no parent item), linked by absolute path: inserted
    // in the test, its path depends on the temporary folder.

    // A trashed article, and a trashed PDF of a live article.
    "INSERT INTO items VALUES (40, 1, '2026-01-01 10:00:00', 1, 'ITEMDEL1'), "
    "(41, 3, '2026-01-01 10:00:00', 1, 'ATTDEL11'), "
    "(60, 3, '2026-01-01 10:00:00', 1, 'ATTTRASH')",
    "INSERT INTO itemAttachments VALUES "
    "(41, 40, 0, 'application/pdf', 'storage:d.pdf', NULL), "
    "(60, 10, 0, 'application/pdf', 'storage:t.pdf', NULL)",
    "INSERT INTO itemData VALUES (40, 1, 9)",
    "INSERT INTO deletedItems VALUES (40, '2026-01-02'), (60, '2026-01-02')",

    // A feed item: not part of the library.
    "INSERT INTO items VALUES (70, 1, '2026-01-01 10:00:00', 3, 'ITEMFEED'), "
    "(71, 3, '2026-01-01 10:00:00', 3, 'ATTFEED1')",
    "INSERT INTO itemAttachments VALUES "
    "(71, 70, 0, 'application/pdf', 'storage:f.pdf', NULL)",
};

/// Zotero's data folder: a zotero.sqlite (left open, so recent rows stay in
/// the WAL as when Zotero runs) and storage/, files/ for linked PDFs.
class Fixture {
public:
  Fixture() {
    dataDir = tmp.filePath("Zotero");
    baseDir = tmp.filePath("Linked");
    QDir().mkpath(dataDir);
    writeFile(dataDir + "/storage/ATTA1111/a.pdf");
    writeFile(baseDir + "/books/b.pdf");
    standalonePdf = tmp.filePath("Elsewhere/loose.pdf");
    writeFile(standalonePdf);
    writeFile(dataDir + "/storage/ATTDEL11/d.pdf");

    QVERIFY(db.open(dataDir + "/zotero.sqlite").has_value());
    QVERIFY(db.exec("PRAGMA wal_autocheckpoint = 0").has_value());
    for (const char *sql : kSchema)
      QVERIFY2(db.exec(sql).has_value(), sql);
    exec(QString("INSERT INTO items VALUES (30, 3, '2026-03-01 08:00:00', 1, 'ATTSTAND')"));
    exec(QString("INSERT INTO itemAttachments VALUES (30, NULL, 2, "
                 "'application/pdf', '%1', NULL)")
             .arg(QDir::toNativeSeparators(standalonePdf)));
    exec("INSERT INTO itemData VALUES (30, 1, 7)");
    exec("INSERT INTO collectionItems VALUES (2, 30, 1)");
    // A group article whose PDF was never downloaded.
    exec("INSERT INTO items VALUES (50, 1, '2026-01-01 10:00:00', 2, 'ITEMG111'), "
         "(51, 3, '2026-01-01 10:00:00', 2, 'ATTG1111')");
    exec("INSERT INTO itemAttachments VALUES "
         "(51, 50, 0, 'application/pdf', 'storage:g.pdf', NULL)");
    exec("INSERT INTO itemData VALUES (50, 1, 8)");
    exec("INSERT INTO collectionItems VALUES (4, 50, 0)");
  }

  void exec(const QString &sql) { QVERIFY2(db.exec(sql).has_value(), qPrintable(sql)); }

  QTemporaryDir tmp;
  QString dataDir, baseDir, standalonePdf;
  SqliteDb db;
};

const ZoteroPdf *findPdf(const ZoteroSnapshot &s, const QString &attachmentKey) {
  for (const ZoteroPdf &p : s.pdfs)
    if (p.attachmentKey == attachmentKey)
      return &p;
  return nullptr;
}

} // namespace

class TestZoteroSqliteSource : public QObject {
  Q_OBJECT

private slots:
  void readsChangesStillInTheWal();
  void librariesAndCollections();
  void pdfsOutOfTheTrash();
  void metadata();
  void paths();
  void leavesTheOriginalAlone();
  void missingDatabase();
  void notZotero();
  void realLibrary();
};

void TestZoteroSqliteSource::readsChangesStillInTheWal() {
  Fixture f;
  QVERIFY(QFileInfo(f.dataDir + "/zotero.sqlite-wal").size() > 0);
  auto snap = ZoteroSqliteSource(f.dataDir, f.baseDir).load();
  QVERIFY2(snap.has_value(), qPrintable(snap ? QString() : snap.error()));
  // Rows written after the schema, never checkpointed into zotero.sqlite.
  QVERIFY(findPdf(*snap, "ATTSTAND"));
  QVERIFY(findPdf(*snap, "ATTG1111"));
}

void TestZoteroSqliteSource::librariesAndCollections() {
  Fixture f;
  auto snap = ZoteroSqliteSource(f.dataDir).load();
  QVERIFY2(snap.has_value(), qPrintable(snap ? QString() : snap.error()));

  QCOMPARE(snap->libraries.size(), 2); // the feed is left out
  QCOMPARE(snap->libraries[0].type, QString("user"));
  QCOMPARE(snap->libraries[1].name, QString("Lab"));

  QStringList keys;
  for (const auto &c : snap->collections)
    keys << c.key;
  QCOMPARE(keys, QStringList({"COLCH2AA", "COLTHES1", "COLSHAR1"}));
  QCOMPARE(snap->collections[0].parentKey, QString("COLTHES1"));
  QCOMPARE(snap->collections[2].libraryId, 2);
}

void TestZoteroSqliteSource::pdfsOutOfTheTrash() {
  Fixture f;
  auto snap = ZoteroSqliteSource(f.dataDir, f.baseDir).load();
  QVERIFY2(snap.has_value(), qPrintable(snap ? QString() : snap.error()));
  QStringList keys;
  for (const auto &p : snap->pdfs)
    keys << p.attachmentKey;
  keys.sort();
  // No web link, no trashed PDF or parent, no feed.
  QCOMPARE(keys, QStringList({"ATTA1111", "ATTA2222", "ATTB1111", "ATTG1111",
                              "ATTSTAND"}));
}

void TestZoteroSqliteSource::metadata() {
  Fixture f;
  auto snap = ZoteroSqliteSource(f.dataDir, f.baseDir).load();
  QVERIFY2(snap.has_value(), qPrintable(snap ? QString() : snap.error()));

  const ZoteroPdf *a = findPdf(*snap, "ATTA1111");
  QVERIFY(a);
  QCOMPARE(a->itemKey, QString("ITEMA111"));
  QCOMPARE(a->meta.title, QString("Strategies"));
  QCOMPARE(a->meta.authors, QStringList({"Ioannou, Christos A.", "Romero, Julian"}));
  QCOMPARE(a->meta.year, QString("2014"));
  QCOMPARE(a->meta.journal, QString("Games"));
  QCOMPARE(a->meta.doi, QString("10.1/x"));
  QCOMPARE(a->meta.itemType, QString("journalArticle"));
  QCOMPARE(a->meta.tags, QStringList({"Absenteeism", "game theory"}));
  QCOMPARE(a->storageHash, QString("abcdef0123"));
  QCOMPARE(a->dateModified, QString("2026-02-01 09:00:00")); // the attachment's
  QStringList colls = a->collectionKeys;
  colls.sort();
  QCOMPARE(colls, QStringList({"COLCH2AA", "COLTHES1"}));

  const ZoteroPdf *b = findPdf(*snap, "ATTB1111");
  QVERIFY(b);
  QCOMPARE(b->meta.authors, QStringList({"Doe, Jane", "World Bank"})); // editors
  QCOMPARE(b->meta.year, QString("2019"));
  QVERIFY(b->collectionKeys.isEmpty()); // only in a trashed collection

  const ZoteroPdf *s = findPdf(*snap, "ATTSTAND");
  QVERIFY(s);
  QCOMPARE(s->itemKey, QString("ATTSTAND"));
  QCOMPARE(s->meta.title, QString("Loose notes"));
  QCOMPARE(s->collectionKeys, QStringList({"COLCH2AA"}));
  QCOMPARE(findPdf(*snap, "ATTG1111")->libraryId, 2);
}

void TestZoteroSqliteSource::paths() {
  Fixture f;
  auto snap = ZoteroSqliteSource(f.dataDir, f.baseDir).load();
  QVERIFY2(snap.has_value(), qPrintable(snap ? QString() : snap.error()));

  const ZoteroPdf *a = findPdf(*snap, "ATTA1111");
  QCOMPARE(a->path, f.dataDir + "/storage/ATTA1111/a.pdf");
  QVERIFY(a->onDisk);
  const ZoteroPdf *a2 = findPdf(*snap, "ATTA2222"); // not downloaded
  QCOMPARE(a2->path, f.dataDir + "/storage/ATTA2222/a2.pdf");
  QVERIFY(!a2->onDisk);
  QCOMPARE(findPdf(*snap, "ATTB1111")->path, f.baseDir + "/books/b.pdf");
  QVERIFY(findPdf(*snap, "ATTB1111")->onDisk);
  QCOMPARE(findPdf(*snap, "ATTSTAND")->path, f.standalonePdf);
  QVERIFY(findPdf(*snap, "ATTSTAND")->onDisk);

  // Without the base folder, a relative linked file cannot be found.
  auto noBase = ZoteroSqliteSource(f.dataDir).load();
  QVERIFY(noBase.has_value());
  QVERIFY(findPdf(*noBase, "ATTB1111")->path.isEmpty());
  QVERIFY(!findPdf(*noBase, "ATTB1111")->onDisk);
}

void TestZoteroSqliteSource::leavesTheOriginalAlone() {
  Fixture f;
  const QStringList before = QDir(f.dataDir).entryList(QDir::Files);
  QVERIFY(ZoteroSqliteSource(f.dataDir).load().has_value());
  QCOMPARE(QDir(f.dataDir).entryList(QDir::Files), before);
}

void TestZoteroSqliteSource::missingDatabase() {
  QTemporaryDir tmp;
  auto snap = ZoteroSqliteSource(tmp.path()).load();
  QVERIFY(!snap.has_value());
  QVERIFY(snap.error().contains("zotero.sqlite"));
}

void TestZoteroSqliteSource::notZotero() {
  QTemporaryDir tmp;
  {
    SqliteDb db;
    QVERIFY(db.open(tmp.filePath("zotero.sqlite")).has_value());
    QVERIFY(db.exec("CREATE TABLE t (x INT)").has_value());
  }
  auto snap = ZoteroSqliteSource(tmp.path()).load();
  QVERIFY(!snap.has_value());
  QVERIFY(snap.error().contains("not a Zotero database"));
}

// A real Zotero library, read only: run with HODEIA_ZOTERO_DIR=<data folder>
// (Zotero may be running).  Skipped otherwise.
void TestZoteroSqliteSource::realLibrary() {
  const QString dir = qEnvironmentVariable("HODEIA_ZOTERO_DIR");
  if (dir.isEmpty())
    QSKIP("HODEIA_ZOTERO_DIR not set");
  QElapsedTimer timer;
  timer.start();
  auto snap = ZoteroSqliteSource(dir, ZoteroLocator::readPrefs().baseAttachmentPath).load();
  QVERIFY2(snap.has_value(), qPrintable(snap ? QString() : snap.error()));
  for (const ZoteroCollection &c : snap->collections) {
    int total = 0, onDisk = 0;
    for (const ZoteroPdf &p : snap->pdfs)
      if (p.collectionKeys.contains(c.key)) {
        ++total;
        onDisk += p.onDisk;
      }
    qInfo().noquote() << QString("%1 [%2] library %3: %4 PDF, %5 on disk")
                             .arg(c.name, c.key).arg(c.libraryId).arg(total).arg(onDisk);
  }
  int noTitle = 0, noAuthor = 0, noYear = 0, noHash = 0;
  for (const ZoteroPdf &p : snap->pdfs) {
    noTitle += p.meta.title.isEmpty();
    noAuthor += p.meta.authors.isEmpty();
    noYear += p.meta.year.isEmpty();
    noHash += p.storageHash.isEmpty();
  }
  qInfo().noquote() << QString("%1 libraries, %2 collections, %3 PDF in %4 ms; "
                               "without title %5, authors %6, year %7, hash %8")
                           .arg(snap->libraries.size()).arg(snap->collections.size())
                           .arg(snap->pdfs.size()).arg(timer.elapsed())
                           .arg(noTitle).arg(noAuthor).arg(noYear).arg(noHash);
  QVERIFY(!snap->pdfs.isEmpty());
}

QTEST_GUILESS_MAIN(TestZoteroSqliteSource)
#include "tst_zoterosqlitesource.moc"
