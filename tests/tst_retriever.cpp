// Unit tests for Retriever::groupByPage: relevance cut, budget filling,
// grouping by page and page images.  No database: chunks are given.

#include "retriever.h"

#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

namespace {

ChunkSearchResult chunk(const QString &hash, int page0, const QString &text,
                        float distance) {
  ChunkSearchResult c;
  c.paperHash = hash;
  c.page = page0;
  c.chunkIdx = 0;
  c.text = text;
  c.distance = distance;
  c.citation = hash + " (2020)";
  c.cleanTitle = "Title " + hash;
  return c;
}

// Every chunk text costs its length in tokens: easy budgets.
int lengthTokens(const QString &text) { return int(text.length()); }

Retriever::Options withBudget(int tokens) {
  Retriever::Options o;
  o.budgetTokens = tokens;
  return o;
}

} // namespace

class TestRetriever : public QObject {
  Q_OBJECT

private slots:
  void empty();
  void groupsByPageInRelevanceOrder();
  void dropsDuplicateChunks();
  void relevanceCutoff();
  void budget();
  void images();
  void candidateCount();
};

void TestRetriever::empty() {
  QVERIFY(Retriever::groupByPage({}, withBudget(10000), lengthTokens).isEmpty());
}

void TestRetriever::groupsByPageInRelevanceOrder() {
  const QList<ChunkSearchResult> chunks = {
      chunk("b", 3, "b3-first", 0.10f), chunk("a", 0, "a0", 0.11f),
      chunk("b", 3, "b3-second", 0.12f)};
  const auto pages = Retriever::groupByPage(chunks, withBudget(10000),
                                            lengthTokens);
  QCOMPARE(pages.size(), 2);
  QCOMPARE(pages[0].paperHash, QString("b"));
  QCOMPARE(pages[0].page, 3);
  QCOMPARE(pages[0].displayPage(), 4);
  QCOMPARE(pages[0].textChunks, QStringList({"b3-first", "b3-second"}));
  QCOMPARE(pages[0].citation, QString("b (2020)"));
  QCOMPARE(pages[0].cleanTitle, QString("Title b"));
  QCOMPARE(pages[1].paperHash, QString("a"));
  QVERIFY(pages[0].imageFile.isEmpty());
  QVERIFY(!pages[0].withImage);
}

void TestRetriever::dropsDuplicateChunks() {
  const auto pages = Retriever::groupByPage(
      {chunk("a", 0, "same", 0.1f), chunk("a", 0, "same", 0.1f)},
      withBudget(10000), lengthTokens);
  QCOMPARE(pages.size(), 1);
  QCOMPARE(pages[0].textChunks.size(), 1);
}

void TestRetriever::relevanceCutoff() {
  // Farther than best + RelevanceMargin: dropped, budget or not.
  const float far = 0.10f + Retriever::RelevanceMargin + 0.01f;
  const auto pages = Retriever::groupByPage(
      {chunk("a", 0, "near", 0.10f), chunk("b", 0, "far", far)},
      withBudget(10000), lengthTokens);
  QCOMPARE(pages.size(), 1);
  QCOMPARE(pages[0].paperHash, QString("a"));
}

void TestRetriever::budget() {
  // A new page costs PageHeaderTokens + text + ChunkSeparatorTokens.
  const int pageCost =
      Retriever::PageHeaderTokens + 100 + Retriever::ChunkSeparatorTokens;
  const QString text100(100, 'x');
  const QString text50(50, 'y');
  // Room for one 100-token page, not a second; a smaller later chunk on the
  // first page still fits.
  const int room = pageCost + 50 + Retriever::ChunkSeparatorTokens + 70;
  const auto pages = Retriever::groupByPage(
      {chunk("a", 0, text100, 0.10f), chunk("b", 0, text100 + "z", 0.11f),
       chunk("a", 0, text50, 0.12f)},
      withBudget(room), lengthTokens);
  QCOMPARE(pages.size(), 1);
  QCOMPARE(pages[0].textChunks, QStringList({text100, text50}));
}

void TestRetriever::images() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QImage img(8, 8, QImage::Format_RGB888);
  img.fill(Qt::white);
  // Page images only for pages 1 and 2 of paper "a" (0-based 0 and 1).
  QVERIFY(img.save(dir.filePath("a_page_001.png")));
  QVERIFY(img.save(dir.filePath("a_page_002.png")));

  Retriever::Options o = withBudget(100000);
  o.imageDir = dir.path();
  const QList<ChunkSearchResult> chunks = {chunk("a", 0, "p1", 0.1f),
                                           chunk("a", 1, "p2", 0.1f),
                                           chunk("a", 2, "p3", 0.1f)};

  // Text mode: image paths found, none sent.
  auto pages = Retriever::groupByPage(chunks, o, lengthTokens);
  QCOMPARE(pages[0].imageFile, dir.filePath("a_page_001.png"));
  QVERIFY(!pages[0].withImage);
  QVERIFY(pages[2].imageFile.isEmpty());

  // Visual mode: images sent for the pages that have one.
  o.useVisual = true;
  pages = Retriever::groupByPage(chunks, o, lengthTokens);
  QVERIFY(pages[0].withImage);
  QVERIFY(pages[1].withImage);
  QVERIFY(!pages[2].withImage);

  // An image only if its ImageTokens still fit in the budget.
  o.budgetTokens = Retriever::PageHeaderTokens + 2 +
                   Retriever::ChunkSeparatorTokens + Retriever::ImageTokens -
                   1;
  pages = Retriever::groupByPage({chunks[0]}, o, lengthTokens);
  QCOMPARE(pages.size(), 1);
  QVERIFY(!pages[0].withImage);
}

void TestRetriever::candidateCount() {
  QCOMPARE(Retriever::candidateCount(0), 80);
  QCOMPARE(Retriever::candidateCount(16384), 117);
  QCOMPARE(Retriever::candidateCount(10'000'000), 2000);
}

QTEST_GUILESS_MAIN(TestRetriever)
#include "tst_retriever.moc"
