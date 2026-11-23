// Unit tests for SummaryPrompt: pages rebuilt from chunks, page images by
// visual mode, and the choice of the system prompt.

#include "summarypromptbuilder.h"

#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

namespace {

ChunkSearchResult chunk(int page0, const QString &text,
                        const QString &metadata = {}) {
  ChunkSearchResult c;
  c.paperHash = "h";
  c.page = page0;
  c.chunkIdx = 0;
  c.text = text;
  c.distance = 0;
  c.pageMetadata = metadata;
  return c;
}


} // namespace

class TestSummaryPrompt : public QObject {
  Q_OBJECT

private slots:
  void noChunks();
  void pagesFromChunks();
  void chunkOverlap();
  void pagesRebuildTheText();
  void imagesByMode_data();
  void imagesByMode();
  void missingImageFallsBackToText();
};

void TestSummaryPrompt::noChunks() {
  const PromptPayload p =
      SummaryPrompt::build("h", {}, VisualMode::Full, QString());
  QCOMPARE(p.userMessage, SummaryPrompt::userPrompt());
  QVERIFY(p.pageBlocks.empty());
}

void TestSummaryPrompt::pagesFromChunks() {
  // Chunks of a page are joined (apart), pages come out in page order.
  const QList<ChunkSearchResult> chunks = {
      chunk(1, "B1."), chunk(0, "A1."), chunk(0, "A2."), chunk(1, "B2.")};
  const PromptPayload p =
      SummaryPrompt::build("h", chunks, VisualMode::TextOnly, QString());

  QCOMPARE(p.pageBlocks.size(), size_t(2));
  QCOMPARE(p.pageBlocks[0].pageNumber, 1);
  QCOMPARE(p.pageBlocks[0].text, QString("A1. A2."));
  QCOMPARE(p.pageBlocks[1].pageNumber, 2);
  QCOMPARE(p.pageBlocks[1].text, QString("B1. B2."));
  QCOMPARE(p.systemPrompt, SummaryPrompt::textSystemPrompt());
  QVERIFY(p.textOnlySystemPrompt.isEmpty());
  QCOMPARE(p.userMessage, QString("Summarize this paper:"));
}

void TestSummaryPrompt::chunkOverlap() {
  using SummaryPrompt::chunkOverlap;
  // Whole sentences carried over.
  QCOMPARE(chunkOverlap(u"One. Two. Three.", u"Two. Three. Four."),
           qsizetype(11));
  QCOMPARE(chunkOverlap(u"One. Two.", u"Two. Three."), qsizetype(4));
  // None (dropped when a long sentence follows).
  QCOMPARE(chunkOverlap(u"One. Two.", u"Three. Four."), qsizetype(0));
  // A common end/start that is not whole sentences is no overlap.
  QCOMPARE(chunkOverlap(u"The data.", u"a. Next"), qsizetype(0));
  QCOMPARE(chunkOverlap(u"x 12", u"12 y"), qsizetype(2));
  QCOMPARE(chunkOverlap(u"x 123", u"23 y"), qsizetype(0));
  // Never the whole next chunk.
  QCOMPARE(chunkOverlap(u"One. Two.", u"Two."), qsizetype(0));
  QCOMPARE(chunkOverlap(u"", u"Two."), qsizetype(0));
}

// Chunks as the ingestion cuts them (sentences, overlap of the last ones):
// the pages give back the text, each sentence once.
void TestSummaryPrompt::pagesRebuildTheText() {
  const QList<ChunkSearchResult> chunks = {
      chunk(0, "S1. S2. S3."),
      chunk(0, "S3. S4. S5."),        // overlap: S3.
      chunk(1, "S4. S5. S6. S7."),    // overlap: S4. S5., new text on page 2
      chunk(1, "S8. S9."),            // no overlap
      chunk(2, "S9. S10.")};
  const QMap<int, QString> pages = SummaryPrompt::pageTexts(chunks);
  QCOMPARE(pages.value(0), QString("S1. S2. S3. S4. S5."));
  QCOMPARE(pages.value(1), QString("S6. S7. S8. S9."));
  QCOMPARE(pages.value(2), QString("S10."));
}

void TestSummaryPrompt::imagesByMode_data() {
  QTest::addColumn<int>("mode");
  QTest::addColumn<QList<bool>>("withImage"); // per page 1, 2, 3
  QTest::newRow("full") << int(VisualMode::Full)
                        << QList<bool>{true, true, true};
  // Reduced: figure on page 1, table on page 3, nothing on page 2.
  QTest::newRow("reduced") << int(VisualMode::Reduced)
                           << QList<bool>{true, false, true};
  QTest::newRow("text only") << int(VisualMode::TextOnly)
                             << QList<bool>{false, false, false};
}

void TestSummaryPrompt::imagesByMode() {
  QFETCH(int, mode);
  QFETCH(QList<bool>, withImage);

  QTemporaryDir dir;
  QImage img(8, 8, QImage::Format_RGB888);
  img.fill(Qt::white);
  for (int n = 1; n <= 3; ++n)
    QVERIFY(img.save(dir.filePath(QString("h_page_%1.png")
                                      .arg(n, 3, 10, QChar('0')))));

  const QList<ChunkSearchResult> chunks = {
      chunk(0, "p1", R"({"has_figure": true})"),
      chunk(1, "p2", R"({"has_figure": false})"),
      chunk(2, "p3", R"({"has_table": true})")};
  const PromptPayload p =
      SummaryPrompt::build("h", chunks, VisualMode(mode), dir.path());

  QCOMPARE(p.pageBlocks.size(), size_t(3));
  bool any = false;
  for (int i = 0; i < 3; ++i) {
    QCOMPARE(!p.pageBlocks[i].visualContext.data.empty(), withImage[i]);
    any = any || withImage[i];
  }
  // The visual prompt only when images made it into the payload, with the
  // text prompt as the fallback for backends that cannot pass them.
  QCOMPARE(p.systemPrompt, any ? SummaryPrompt::visualSystemPrompt()
                               : SummaryPrompt::textSystemPrompt());
  QCOMPARE(p.textOnlySystemPrompt,
           any ? SummaryPrompt::textSystemPrompt() : QString());
}

void TestSummaryPrompt::missingImageFallsBackToText() {
  QTemporaryDir dir; // no page images in it
  const PromptPayload p = SummaryPrompt::build("h", {chunk(0, "p1")},
                                               VisualMode::Full, dir.path());
  QVERIFY(p.pageBlocks[0].visualContext.data.empty());
  QCOMPARE(p.systemPrompt, SummaryPrompt::textSystemPrompt());
}

QTEST_GUILESS_MAIN(TestSummaryPrompt)
#include "tst_summarypromptbuilder.moc"
