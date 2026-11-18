// Unit tests for ZoteroRepository: the Zotero tables of a project database
// (followed collections, PDFs with their status and metadata).

#include "projectstore.h"
#include "sqlitedb.h"
#include "zoterorepository.h"

#include <QTemporaryDir>
#include <QtTest>

namespace {

ZoteroItemRow item(const QString &attachmentKey, const QString &hash,
                   int libraryId = 1) {
  ZoteroItemRow row;
  row.libraryId = libraryId;
  row.itemKey = "ITEM" + attachmentKey;
  row.attachmentKey = attachmentKey;
  row.paperHash = hash;
  row.meta.title = "Title " + attachmentKey;
  row.meta.authors = {"Christos A. Ioannou", "Julian Romero, Jr."};
  row.meta.year = "2014";
  row.meta.journal = "Games";
  row.meta.doi = "10.1/x";
  row.meta.itemType = "journalArticle";
  row.meta.tags = {"game theory"};
  row.dateModified = "2026-01-01 10:00:00";
  return row;
}

} // namespace

class TestZoteroRepository : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();
  void followedCollections();
  void insertAndRead();
  void upsertKeepsStatus();
  void statusAndRemoval();
  void itemByHash();
  void statusStrings();
  void metaByHash();

private:
  QTemporaryDir *m_dir = nullptr;
  SqliteDb m_db;
};

void TestZoteroRepository::init() {
  m_dir = new QTemporaryDir;
  QVERIFY(ProjectStore::open(m_db, m_dir->filePath("P.db")).has_value());
}

void TestZoteroRepository::cleanup() {
  m_db.close();
  delete m_dir;
  m_dir = nullptr;
}

void TestZoteroRepository::followedCollections() {
  ZoteroRepository repo(m_db);
  QVERIFY(repo.followedCollections().isEmpty());
  QVERIFY(repo.setFollowedCollections({{1, "COLTHES1", "Thesis"},
                                       {2, "COLSHAR1", "Shared"},
                                       {1, "COLCH2AA", "Chapter 2"}}));
  auto list = repo.followedCollections();
  QCOMPARE(list.size(), 3);
  QCOMPARE(list[0].name, QString("Chapter 2")); // by library, then name
  QCOMPARE(list[2].libraryId, 2);

  // Replaced as a whole.
  QVERIFY(repo.setFollowedCollections({{1, "COLTHES1", "Thesis"}}));
  QCOMPARE(repo.followedCollections().size(), 1);
  // A duplicate fails, and leaves the previous list.
  QVERIFY(!repo.setFollowedCollections({{1, "K", "a"}, {1, "K", "b"}}));
  QCOMPARE(repo.followedCollections().size(), 1);
  QCOMPARE(repo.followedCollections()[0].key, QString("COLTHES1"));
}

void TestZoteroRepository::insertAndRead() {
  ZoteroRepository repo(m_db);
  auto id = repo.upsertItem(item("ATTA1111", "hash-a"));
  QVERIFY2(id.has_value(), qPrintable(id ? QString() : id.error()));
  QVERIFY(*id > 0);

  const auto rows = repo.items();
  QCOMPARE(rows.size(), 1);
  const ZoteroItemRow &r = rows[0];
  QCOMPARE(r.id, *id);
  QCOMPARE(r.itemKey, QString("ITEMATTA1111"));
  QCOMPARE(r.paperHash, QString("hash-a"));
  // Names with commas survive (JSON arrays).
  QCOMPARE(r.meta.authors, QStringList({"Christos A. Ioannou", "Julian Romero, Jr."}));
  QCOMPARE(r.meta.tags, QStringList({"game theory"}));
  QCOMPARE(r.meta.journal, QString("Games"));
  QCOMPARE(r.dateModified, QString("2026-01-01 10:00:00"));
  QCOMPARE(r.status, ZoteroStatus::Pending);
  QVERIFY(!r.removedAt.isValid());
}

// Zotero changed the PDF (new file, new metadata): the row follows, but the
// user's decision (ingested / ignored) stays, and a removal is undone.
void TestZoteroRepository::upsertKeepsStatus() {
  ZoteroRepository repo(m_db);
  const int id = *repo.upsertItem(item("ATTA1111", "hash-a"));
  QVERIFY(repo.setStatus({id}, ZoteroStatus::Ignored));
  QVERIFY(repo.markRemoved({id}, QDateTime::currentDateTimeUtc()));

  ZoteroItemRow changed = item("ATTA1111", "hash-b");
  changed.meta.title = "New title";
  changed.status = ZoteroStatus::Pending; // ignored for a known row
  QCOMPARE(*repo.upsertItem(changed), id);

  const auto rows = repo.items();
  QCOMPARE(rows.size(), 1);
  QCOMPARE(rows[0].paperHash, QString("hash-b"));
  QCOMPARE(rows[0].meta.title, QString("New title"));
  QCOMPARE(rows[0].status, ZoteroStatus::Ignored);
  QVERIFY(!rows[0].removedAt.isValid());

  // Same attachment key in another library: another row.
  QVERIFY(*repo.upsertItem(item("ATTA1111", "hash-b", 2)) != id);
  QCOMPARE(repo.items().size(), 2);
}

void TestZoteroRepository::statusAndRemoval() {
  ZoteroRepository repo(m_db);
  const int a = *repo.upsertItem(item("A", "h1"));
  const int b = *repo.upsertItem(item("B", "h2"));
  QVERIFY(repo.setStatus({a, b}, ZoteroStatus::Ingested));
  for (const auto &r : repo.items())
    QCOMPARE(r.status, ZoteroStatus::Ingested);

  const QDateTime first(QDate(2026, 1, 2), QTime(10, 0), QTimeZone::UTC);
  QVERIFY(repo.markRemoved({a}, first));
  QVERIFY(repo.markRemoved({a, b}, first.addDays(3)));
  const auto rows = repo.items();
  QCOMPARE(rows[0].removedAt, first); // the first date is kept
  QCOMPARE(rows[1].removedAt, first.addDays(3));
}

void TestZoteroRepository::itemByHash() {
  ZoteroRepository repo(m_db);
  QVERIFY(!repo.itemByHash("none").has_value());

  // The same file in a group and in the user's library.
  ZoteroItemRow group = item("G", "same", 2);
  group.meta.title = "From the group";
  const int g = *repo.upsertItem(group);
  ZoteroItemRow mine = item("U", "same", 1);
  mine.meta.title = "From my library";
  const int u = *repo.upsertItem(mine);

  QCOMPARE(repo.itemByHash("same")->meta.title, QString("From my library"));
  // Gone from the user's library: the group's copy, still there, wins.
  QVERIFY(repo.markRemoved({u}, QDateTime::currentDateTimeUtc()));
  QCOMPARE(repo.itemByHash("same")->id, g);
  // Gone everywhere: the metadata is still there for display.
  QVERIFY(repo.markRemoved({g}, QDateTime::currentDateTimeUtc()));
  QVERIFY(repo.itemByHash("same").has_value());
}

void TestZoteroRepository::statusStrings() {
  for (ZoteroStatus s :
       {ZoteroStatus::Pending, ZoteroStatus::Ingested, ZoteroStatus::Ignored})
    QCOMPARE(zoteroStatusFromString(zoteroStatusName(s)), std::optional(s));
  QVERIFY(!zoteroStatusFromString("lost").has_value());
}

// Same preference as itemByHash(), for every hash at once.
void TestZoteroRepository::metaByHash() {
  ZoteroRepository repo(m_db);
  QVERIFY(repo.metaByHash().isEmpty());
  ZoteroItemRow group = item("G", "same", 2);
  group.meta.title = "From the group";
  QVERIFY(repo.upsertItem(group).has_value());
  ZoteroItemRow mine = item("U", "same", 1);
  mine.meta.title = "From my library";
  const int u = *repo.upsertItem(mine);
  QVERIFY(repo.upsertItem(item("O", "other")).has_value());

  auto metas = repo.metaByHash();
  QCOMPARE(metas.size(), 2);
  QCOMPARE(metas.value("same").title, QString("From my library"));
  QCOMPARE(metas.value("other").title, QString("Title O"));

  QVERIFY(repo.markRemoved({u}, QDateTime::currentDateTimeUtc()));
  QCOMPARE(repo.metaByHash().value("same").title, QString("From the group"));
}

QTEST_GUILESS_MAIN(TestZoteroRepository)
#include "tst_zoterorepository.moc"
