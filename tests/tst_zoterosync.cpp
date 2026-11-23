// Unit tests for ZoteroSync: what the project does with the PDFs of the
// followed Zotero collections (store, propose, mark imported or removed).

#include "zoterosync.h"

#include <QtTest>

using namespace ZoteroSync;

namespace {

ZoteroPdf pdf(const QString &key, const QStringList &collections,
              bool onDisk = true, int libraryId = 1) {
  ZoteroPdf p;
  p.libraryId = libraryId;
  p.itemKey = "I" + key;
  p.attachmentKey = key;
  p.path = "/zotero/storage/" + key + "/f.pdf";
  p.onDisk = onDisk;
  p.dateModified = "2026-01-01 10:00:00";
  p.collectionKeys = collections;
  p.meta.title = "Title " + key;
  return p;
}

/// The row the project holds for `p`, with `hash` and `status`.
ZoteroItemRow known(int id, const ZoteroPdf &p, const QString &hash,
                    ZoteroStatus status) {
  ZoteroItemRow r;
  r.id = id;
  r.libraryId = p.libraryId;
  r.itemKey = p.itemKey;
  r.attachmentKey = p.attachmentKey;
  r.paperHash = hash;
  r.meta = p.meta;
  r.dateModified = p.dateModified;
  r.status = status;
  return r;
}

/// A hash function that records the files it reads.
struct FakeMd5 {
  QStringList *read;
  QString operator()(const QString &path) const {
    read->append(path);
    return "md5:" + path.section('/', 3, 3);
  }
};

} // namespace

class TestZoteroSync : public QObject {
  Q_OBJECT

private slots:
  void followedOnce();
  void hashesReuseTheProjects();
  void newPdfs();
  void unchangedKnownPdf();
  void changedInZotero();
  void importedByHandLater();
  void replacedFile();
  void deletedFromProjectGoesToIgnored();
  void removedAndBack();
  void notDownloadedIsNotRemoved();
  void proposalsFilter();
  void ignoredFilter();
  void counts();
};

void TestZoteroSync::followedOnce() {
  ZoteroSnapshot snap;
  snap.pdfs = {pdf("A", {"C1", "C2"}), pdf("B", {"C3"}), pdf("G", {"C1"}, true, 2)};
  // A sits in two followed collections: listed once.  C1 of library 2 is
  // another collection than C1 of library 1.
  const auto out = followedPdfs(snap, {{1, "C1", "c1"}, {1, "C2", "c2"}});
  QCOMPARE(out.size(), 1);
  QCOMPARE(out[0].attachmentKey, QString("A"));
}

void TestZoteroSync::hashesReuseTheProjects() {
  QStringList read;
  const ZoteroPdf a = pdf("A", {}), b = pdf("B", {}), off = pdf("C", {}, false);
  ZoteroPdf touched = pdf("D", {});
  const QList<ZoteroItemRow> rows{
      known(1, a, "stored-a", ZoteroStatus::Ingested),
      known(2, touched, "old-d", ZoteroStatus::Ingested)};
  touched.dateModified = "2026-05-01 00:00:00"; // changed in Zotero since

  const auto hashes = resolveHashes({a, b, off, touched}, rows, FakeMd5{&read});
  QCOMPARE(hashes.value("1:A"), QString("stored-a")); // not read again
  QCOMPARE(hashes.value("1:B"), QString("md5:B"));
  QCOMPARE(hashes.value("1:D"), QString("md5:D"));
  QVERIFY(!hashes.contains("1:C")); // not on disk
  QCOMPARE(read.size(), 2);
}

void TestZoteroSync::newPdfs() {
  const ZoteroPdf a = pdf("A", {"C"}), b = pdf("B", {"C"});
  const auto p = plan({a, b}, {}, {{"1:A", "ha"}, {"1:B", "hb"}}, {"hb"});
  QCOMPARE(p.store.size(), 2);
  QCOMPARE(p.store[0].status, ZoteroStatus::Pending);
  QCOMPARE(p.store[0].paperHash, QString("ha"));
  QCOMPARE(p.store[0].meta.title, QString("Title A"));
  // B was imported by hand before the connector: recorded as imported.
  QCOMPARE(p.store[1].status, ZoteroStatus::Ingested);
  QVERIFY(p.ingestedIds.isEmpty() && p.removedIds.isEmpty() && p.missing.isEmpty());
}

void TestZoteroSync::unchangedKnownPdf() {
  const ZoteroPdf a = pdf("A", {"C"});
  const auto p = plan({a}, {known(7, a, "ha", ZoteroStatus::Ignored)},
                      {{"1:A", "ha"}}, {});
  QVERIFY(p.store.isEmpty());
  QVERIFY(p.ingestedIds.isEmpty() && p.pendingIds.isEmpty() && p.removedIds.isEmpty());
}

void TestZoteroSync::changedInZotero() {
  ZoteroPdf a = pdf("A", {"C"});
  const auto row = known(7, a, "ha", ZoteroStatus::Ignored);
  a.meta.title = "Corrected title";
  a.dateModified = "2026-02-01 00:00:00";
  const auto p = plan({a}, {row}, {{"1:A", "ha"}}, {});
  QCOMPARE(p.store.size(), 1);
  QCOMPARE(p.store[0].id, 7);
  QCOMPARE(p.store[0].meta.title, QString("Corrected title"));
  QCOMPARE(p.store[0].status, ZoteroStatus::Ignored); // the user's choice stays
}

void TestZoteroSync::importedByHandLater() {
  const ZoteroPdf a = pdf("A", {"C"});
  const auto p = plan({a}, {known(7, a, "ha", ZoteroStatus::Pending)},
                      {{"1:A", "ha"}}, {"ha"});
  QCOMPARE(p.ingestedIds, QList<int>({7}));
}

void TestZoteroSync::replacedFile() {
  const ZoteroPdf a = pdf("A", {"C"});
  const auto p = plan({a}, {known(7, a, "old", ZoteroStatus::Ingested)},
                      {{"1:A", "new"}}, {"old"});
  QCOMPARE(p.pendingIds, QList<int>({7}));
  QCOMPARE(p.store.size(), 1);
  QCOMPARE(p.store[0].paperHash, QString("new"));
}

// Deleted from the project: not proposed again, but listed as ignored.
void TestZoteroSync::deletedFromProjectGoesToIgnored() {
  const ZoteroPdf a = pdf("A", {"C"});
  const auto row = known(7, a, "ha", ZoteroStatus::Ingested);
  const auto p = plan({a}, {row}, {{"1:A", "ha"}}, {});
  QVERIFY(p.pendingIds.isEmpty());
  QCOMPARE(p.ignoredIds, QList<int>({7}));
  QVERIFY(p.store.isEmpty());

  auto ignoredRow = row;
  ignoredRow.status = ZoteroStatus::Ignored;
  QCOMPARE(ZoteroSync::ignored({ignoredRow}, {}).size(), 1);
  // Still in the project: nothing to do.
  QVERIFY(plan({a}, {row}, {{"1:A", "ha"}}, {"ha"}).ignoredIds.isEmpty());
}

void TestZoteroSync::removedAndBack() {
  const ZoteroPdf a = pdf("A", {"C"}), gone = pdf("G", {"C"});
  auto back = known(8, a, "ha", ZoteroStatus::Ingested);
  back.removedAt = QDateTime::currentDateTimeUtc();
  auto alreadyGone = known(9, pdf("H", {}), "hh", ZoteroStatus::Ingested);
  alreadyGone.removedAt = QDateTime::currentDateTimeUtc();

  const auto p = plan({a}, {back, known(7, gone, "hg", ZoteroStatus::Ingested), alreadyGone},
                      {{"1:A", "ha"}}, {"ha", "hg"});
  QCOMPARE(p.removedIds, QList<int>({7})); // 9 is already marked
  QCOMPARE(p.store.size(), 1); // A came back: stored again, mark cleared
  QCOMPARE(p.store[0].id, 8);
}

// A PDF that Zotero has not downloaded is still in the collection: it is
// reported missing, not removed.
void TestZoteroSync::notDownloadedIsNotRemoved() {
  const ZoteroPdf a = pdf("A", {"C"}, false);
  const auto p = plan({a}, {known(7, a, "ha", ZoteroStatus::Ingested)}, {}, {"ha"});
  QVERIFY(p.removedIds.isEmpty());
  QCOMPARE(p.missing.size(), 1);
  QVERIFY(p.store.isEmpty());
}

void TestZoteroSync::proposalsFilter() {
  const ZoteroPdf p1 = pdf("A", {}), p2 = pdf("B", {}), p3 = pdf("C", {}),
                  p4 = pdf("D", {});
  auto removed = known(4, p4, "hd", ZoteroStatus::Pending);
  removed.removedAt = QDateTime::currentDateTimeUtc();
  const auto out = proposals({known(1, p1, "ha", ZoteroStatus::Pending),
                              known(2, p2, "hb", ZoteroStatus::Ignored),
                              known(3, p3, "hc", ZoteroStatus::Pending), removed},
                             {"hc"});
  QCOMPARE(out.size(), 1);
  QCOMPARE(out[0].id, 1);
}

void TestZoteroSync::ignoredFilter() {
  const ZoteroPdf p1 = pdf("A", {}), p2 = pdf("B", {}), p3 = pdf("C", {}),
                  p4 = pdf("D", {});
  auto removed = known(4, p4, "hd", ZoteroStatus::Ignored);
  removed.removedAt = QDateTime::currentDateTimeUtc();
  // Pending (1), ignored but in the project since (3), gone from Zotero (4):
  // not listed.
  const auto out = ignored({known(1, p1, "ha", ZoteroStatus::Pending),
                            known(2, p2, "hb", ZoteroStatus::Ignored),
                            known(3, p3, "hc", ZoteroStatus::Ignored), removed},
                           {"hc"});
  QCOMPARE(out.size(), 1);
  QCOMPARE(out[0].id, 2);
}

void TestZoteroSync::counts() {
  ZoteroSnapshot snap;
  ZoteroPdf synced = pdf("B", {"C1"});
  synced.storageHash = "hb";
  snap.pdfs = {pdf("A", {"C1", "C2"}), synced, pdf("M", {"C1"}, false),
               pdf("G", {"C1"}, true, 2)};
  const auto c = countByCollection(
      snap, {known(1, snap.pdfs[0], "ha", ZoteroStatus::Ingested)}, {"ha", "hb"});
  QCOMPARE(c.value("1:C1").pdfs, 3);
  QCOMPARE(c.value("1:C1").onDisk, 2);
  QCOMPARE(c.value("1:C1").inProject, 2); // A by its stored hash, B by Zotero's
  QCOMPARE(c.value("1:C2").pdfs, 1);
  QCOMPARE(c.value("2:C1").pdfs, 1);
  QCOMPARE(c.value("2:C1").inProject, 0);
}

QTEST_GUILESS_MAIN(TestZoteroSync)
#include "tst_zoterosync.moc"
