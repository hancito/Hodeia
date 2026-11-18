// Unit tests for QueryPrompt: the exact layout of a query prompt.

#include "querypromptbuilder.h"

#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

namespace {

PageContext page(const QString &hash, int page0, const QStringList &chunks,
                 const QString &citation, const QString &title) {
  PageContext pc;
  pc.paperHash = hash;
  pc.page = page0;
  pc.textChunks = chunks;
  pc.citation = citation;
  pc.cleanTitle = title;
  return pc;
}

} // namespace

class TestQueryPrompt : public QObject {
  Q_OBJECT

private slots:
  void textLayout();
  void fixedText();
  void images();
};

void TestQueryPrompt::textLayout() {
  const QList<PageContext> pages = {
      page("aaa", 4, {"chunk one", "chunk two"},
           "J. Walker, M. Ben-Akiva (2002)", "Hybrid Choice"),
      page("bbb", 0, {"other"}, "", "Bull - 1984 - Equilibrium")};
  const PromptPayload p = QueryPrompt::build(pages, "What is it?", false);

  QCOMPARE(p.systemPrompt, QueryPrompt::systemPrompt());
  QVERIFY(p.systemPrompt.startsWith("You are an expert academic research"));
  QVERIFY(p.textOnlySystemPrompt.isEmpty());

  const QString block1 =
      "=== PAGE CONTEXT [1] — Walker & Ben-Akiva (2002) — Hybrid Choice, "
      "page 5 ===\nchunk one\n[---]\nchunk two\n[---]\n\n";
  const QString block2 = "=== PAGE CONTEXT [2] — Bull (1984) — Equilibrium, "
                         "page 1 ===\nother\n[---]\n\n";
  QCOMPARE(p.textContext, block1 + "\n" + block2);

  QCOMPARE(p.pageBlocks.size(), size_t(2));
  QCOMPARE(p.pageBlocks[0].pageNumber, 1);
  QCOMPARE(p.pageBlocks[0].text, block1);
  QCOMPARE(p.pageBlocks[1].pageNumber, 2);
  QCOMPARE(p.pageBlocks[1].text, block2);
  QVERIFY(p.pageBlocks[0].visualContext.data.empty());
  QVERIFY(p.visualContext.empty());

  QCOMPARE(p.userMessage,
           "Core Task Query: What is it?\n\n" + QueryPrompt::formatReminder() +
               "\n\nSynthesize your final cross-paper response now:");
}

void TestQueryPrompt::fixedText() {
  QCOMPARE(QueryPrompt::fixedText("Q"), QueryPrompt::systemPrompt() +
                                            QueryPrompt::formatReminder() +
                                            "Q");
}

void TestQueryPrompt::images() {
  QTemporaryDir dir;
  QImage img(16, 8, QImage::Format_RGB888);
  img.fill(Qt::gray);
  const QString file = dir.filePath("aaa_page_001.png");
  QVERIFY(img.save(file));

  PageContext withImage = page("aaa", 0, {"t"}, "", "T");
  withImage.imageFile = file;
  withImage.withImage = true;
  PageContext notSent = withImage; // image found but over the cap/budget
  notSent.withImage = false;

  // Visual mode: only the page marked withImage carries its image.
  PromptPayload p = QueryPrompt::build({withImage, notSent}, "Q", true);
  QCOMPARE(p.pageBlocks[0].visualContext.width, 16);
  QCOMPARE(p.pageBlocks[0].visualContext.height, 8);
  QVERIFY(p.pageBlocks[1].visualContext.data.empty());
  QCOMPARE(p.visualContext.size(), size_t(1));

  // Text mode: no image at all.
  p = QueryPrompt::build({withImage}, "Q", false);
  QVERIFY(p.pageBlocks[0].visualContext.data.empty());
  QVERIFY(p.visualContext.empty());
}

QTEST_GUILESS_MAIN(TestQueryPrompt)
#include "tst_querypromptbuilder.moc"
