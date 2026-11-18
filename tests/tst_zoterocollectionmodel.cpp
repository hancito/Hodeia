// Unit tests for ZoteroCollectionModel: tree order, three-state check boxes
// and what toggling a row follows.

#include "zoterocollectionmodel.h"

#include <QSignalSpy>
#include <QtTest>

namespace {

// Library 1: Thesis > (Chapter 1, Chapter 2 > Drafts), Archive.
// Library 2 (group "Lab"): Shared.
ZoteroSnapshot snapshot() {
  ZoteroSnapshot s;
  s.libraries = {{1, "user", ""}, {2, "group", "Lab"}};
  s.collections = {{1, "THES", "Thesis", ""},     {1, "CH2", "Chapter 2", "THES"},
                   {1, "ARCH", "archive", ""},    {1, "CH1", "Chapter 1", "THES"},
                   {1, "DRAF", "Drafts", "CH2"},  {2, "SHAR", "Shared", ""}};
  return s;
}

QStringList names(const ZoteroCollectionModel &m) {
  QStringList out;
  for (int i = 0; i < m.rowCount(); ++i) {
    const QModelIndex idx = m.index(i);
    out << QString(m.data(idx, ZoteroCollectionModel::DepthRole).toInt(), '-') +
               m.data(idx, ZoteroCollectionModel::NameRole).toString();
  }
  return out;
}

Qt::CheckState state(const ZoteroCollectionModel &m, int row) {
  return Qt::CheckState(m.data(m.index(row), ZoteroCollectionModel::CheckStateRole).toInt());
}

QStringList followedKeys(const ZoteroCollectionModel &m) {
  QStringList out;
  for (const auto &f : m.followed())
    out << QString::number(f.libraryId) + ":" + f.key;
  return out;
}

} // namespace

class TestZoteroCollectionModel : public QObject {
  Q_OBJECT

private slots:
  void treeOrder();
  void countsAndFollowed();
  void toggleSubtree();
  void partialParent();
  void toggleLibrary();
};

void TestZoteroCollectionModel::treeOrder() {
  ZoteroCollectionModel m;
  m.reset(snapshot(), {}, {});
  QCOMPARE(names(m), QStringList({"My Library", "-archive", "-Thesis", "--Chapter 1",
                                  "--Chapter 2", "---Drafts", "Lab", "-Shared"}));
  QVERIFY(m.data(m.index(0), ZoteroCollectionModel::IsLibraryRole).toBool());
  QVERIFY(!m.data(m.index(1), ZoteroCollectionModel::IsLibraryRole).toBool());
}

void TestZoteroCollectionModel::countsAndFollowed() {
  ZoteroCollectionModel m;
  QHash<QString, ZoteroSync::CollectionCounts> counts{{"1:THES", {5, 3, 2}}};
  // An unknown followed collection (deleted in Zotero) is dropped.
  m.reset(snapshot(), counts, {{1, "THES", "Thesis"}, {1, "GONE", "Gone"}});
  QCOMPARE(m.data(m.index(2), ZoteroCollectionModel::PdfCountRole).toInt(), 5);
  QCOMPARE(m.data(m.index(2), ZoteroCollectionModel::OnDiskCountRole).toInt(), 3);
  QCOMPARE(m.data(m.index(2), ZoteroCollectionModel::InProjectCountRole).toInt(), 2);
  QCOMPARE(m.followedCount(), 1);
  QCOMPARE(followedKeys(m), QStringList({"1:THES"}));
}

void TestZoteroCollectionModel::toggleSubtree() {
  ZoteroCollectionModel m;
  m.reset(snapshot(), {}, {});
  QSignalSpy spy(&m, &ZoteroCollectionModel::followedChanged);
  m.toggle(2); // Thesis: itself and its three sub-collections
  QCOMPARE(spy.count(), 1);
  QCOMPARE(followedKeys(m), QStringList({"1:THES", "1:CH1", "1:CH2", "1:DRAF"}));
  QCOMPARE(state(m, 2), Qt::Checked);
  QCOMPARE(state(m, 5), Qt::Checked);
  QCOMPARE(state(m, 0), Qt::PartiallyChecked); // archive is not followed
  m.toggle(2);
  QVERIFY(m.followed().isEmpty());
  QCOMPARE(state(m, 0), Qt::Unchecked);
}

void TestZoteroCollectionModel::partialParent() {
  ZoteroCollectionModel m;
  m.reset(snapshot(), {}, {});
  m.toggle(2);  // Thesis and all below
  m.toggle(5);  // then untick Drafts alone
  QCOMPARE(followedKeys(m), QStringList({"1:THES", "1:CH1", "1:CH2"}));
  QCOMPARE(state(m, 4), Qt::PartiallyChecked); // Chapter 2
  QCOMPARE(state(m, 2), Qt::PartiallyChecked); // Thesis
  // Ticking a partial row completes it.
  m.toggle(2);
  QCOMPARE(state(m, 2), Qt::Checked);
  QCOMPARE(m.followedCount(), 4);
}

void TestZoteroCollectionModel::toggleLibrary() {
  ZoteroCollectionModel m;
  m.reset(snapshot(), {}, {});
  m.toggle(6); // Lab
  QCOMPARE(followedKeys(m), QStringList({"2:SHAR"}));
  QCOMPARE(state(m, 6), Qt::Checked);
  QCOMPARE(state(m, 0), Qt::Unchecked);
  m.toggle(0); // My Library
  QCOMPARE(m.followedCount(), 6);
}

QTEST_GUILESS_MAIN(TestZoteroCollectionModel)
#include "tst_zoterocollectionmodel.moc"
