// Unit tests for MetadataResolver: how papers are named in citations.

#include "libraryrepository.h"
#include "metadataresolver.h"
#include "summaryformat.h"

#include <QtTest>

class TestMetadataResolver : public QObject {
  Q_OBJECT

private slots:
  void compactAuthors_data();
  void compactAuthors();
  void paperLabel_data();
  void paperLabel();
  void forms();
  void zoteroNames();
  void labelWithZotero();
  void summaryWithZotero();
  void pagesWithZotero();
};

void TestMetadataResolver::compactAuthors_data() {
  QTest::addColumn<QString>("authors");
  QTest::addColumn<QString>("expected");
  QTest::newRow("one") << "Clive Bull" << "Bull";
  QTest::newRow("two") << "J. Walker, M. Ben-Akiva" << "Walker & Ben-Akiva";
  QTest::newRow("three") << "A. Walker; M. Ben-Akiva; C. Marley"
                         << "Walker et al.";
  QTest::newRow("empty") << "  " << "";
}

void TestMetadataResolver::compactAuthors() {
  QFETCH(QString, authors);
  QFETCH(QString, expected);
  QCOMPARE(MetadataResolver::compactAuthors(authors), expected);
}

void TestMetadataResolver::paperLabel_data() {
  QTest::addColumn<QString>("title");
  QTest::addColumn<QString>("citation");
  QTest::addColumn<QString>("authors");
  QTest::addColumn<QString>("year");
  QTest::addColumn<QString>("fullTitle");

  // Summarized: "Authors (year)" in the citation.
  QTest::newRow("summarized")
      << "Hybrid Choice Models" << "J. Walker, M. Ben-Akiva (2002)"
      << "Walker & Ben-Akiva" << "2002" << "Hybrid Choice Models";
  // The summary had no year: stored as "(0)".
  QTest::newRow("year 0") << "T" << "Clive Bull (0)" << "Bull" << ""
                          << "T";
  // Citation in another form: kept, elided to 60 characters.
  QTest::newRow("free citation") << "T" << "Some citation" << "Some citation"
                                 << "" << "T";
  // Not summarized: file name in the Zotero form.
  QTest::newRow("zotero file name")
      << "Bull - 1984 - Equilibrium Unemployment"
      << "Bull - 1984 - Equilibrium Unemployment" << "Bull" << "1984"
      << "Equilibrium Unemployment";
  QTest::newRow("zotero, no year")
      << "Bull - Equilibrium Unemployment" << "" << "Bull" << ""
      << "Equilibrium Unemployment";
  QTest::newRow("bare file name") << "Zanardelli et al" << "" << "Zanardelli et al"
                                  << "" << "";
  QTest::newRow("nothing") << "" << "" << "0123456789ab…" << "" << "";
}

void TestMetadataResolver::paperLabel() {
  QFETCH(QString, title);
  QFETCH(QString, citation);
  QFETCH(QString, authors);
  QFETCH(QString, year);
  QFETCH(QString, fullTitle);
  const PaperLabel l = MetadataResolver::paperLabel(
      "0123456789abcdef0123456789abcdef", title, citation);
  QCOMPARE(l.authors, authors);
  QCOMPARE(l.year, year);
  QCOMPARE(l.title, fullTitle);
}

void TestMetadataResolver::forms() {
  const PaperLabel l{"Walker & Ben-Akiva", "2002", "Hybrid Choice Models"};
  QCOMPARE(l.inlineForm(), QString("Walker & Ben-Akiva, 2002"));
  QCOMPARE(l.footerForm(), QString("Walker & Ben-Akiva (2002)"));
  QCOMPARE(l.sourceForm(),
           QString("Walker & Ben-Akiva (2002) — Hybrid Choice Models"));
  const PaperLabel bare{"Zanardelli et al", "", ""};
  QCOMPARE(bare.inlineForm(), QString("Zanardelli et al"));
  QCOMPARE(bare.sourceForm(), QString("Zanardelli et al"));
}

namespace {

ZoteroMeta zoteroMeta() {
  ZoteroMeta z;
  z.title = "Strategies in the  Repeated Game";
  z.authors = {"Ioannou, Christos A.", "Romero, Julian", "Smith, Ann"};
  z.year = "2014";
  z.journal = "Games and Economic Behavior";
  return z;
}

} // namespace

void TestMetadataResolver::zoteroNames() {
  using MetadataResolver::compactAuthorList;
  QCOMPARE(compactAuthorList({"Walker, Joan"}), QString("Walker"));
  QCOMPARE(compactAuthorList({"Walker, Joan", "Ben-Akiva, Moshe"}),
           QString("Walker & Ben-Akiva"));
  QCOMPARE(compactAuthorList(zoteroMeta().authors), QString("Ioannou et al."));
  QCOMPARE(compactAuthorList({"World Bank"}), QString("World Bank")); // institution
  QCOMPARE(compactAuthorList({}), QString());
  QCOMPARE(MetadataResolver::displayName("Ioannou, Christos A."),
           QString("Christos A. Ioannou"));
  QCOMPARE(MetadataResolver::displayName("World Bank"), QString("World Bank"));
}

void TestMetadataResolver::labelWithZotero() {
  const PaperLabel fromFile =
      MetadataResolver::paperLabel("abcdef", "Ioannou - 2014 - Strategies", "");
  const PaperLabel l = MetadataResolver::withZotero(fromFile, zoteroMeta());
  QCOMPARE(l.authors, QString("Ioannou et al."));
  QCOMPARE(l.year, QString("2014"));
  QCOMPARE(l.title, QString("Strategies in the Repeated Game"));

  // A field Zotero lacks keeps the summary's.
  ZoteroMeta partial;
  partial.title = "Only a title";
  const PaperLabel summary{"Walker & Ben-Akiva", "2002", "Summary title"};
  const PaperLabel p = MetadataResolver::withZotero(summary, partial);
  QCOMPARE(p.authors, QString("Walker & Ben-Akiva"));
  QCOMPARE(p.year, QString("2002"));
  QCOMPARE(p.title, QString("Only a title"));
}

void TestMetadataResolver::summaryWithZotero() {
  PaperSummary s;
  s.title = "summary title";
  s.authors = {"C. Ioannou"};
  s.year = 2013;
  s.journal = "";
  s.methodology = "kept";
  MetadataResolver::applyZotero(s, zoteroMeta());
  QCOMPARE(s.title, QString("Strategies in the Repeated Game"));
  QCOMPARE(s.authors, QStringList({"Christos A. Ioannou", "Julian Romero", "Ann Smith"}));
  QCOMPARE(s.year, 2014);
  QCOMPARE(s.journal, QString("Games and Economic Behavior"));
  QCOMPARE(s.methodology, QString("kept")); // the content is the summary's

  SummaryRow row;
  row.title = "t";
  row.year = "2013";
  ZoteroMeta noYear = zoteroMeta();
  noYear.year.clear();
  MetadataResolver::applyZotero(row, noYear);
  QCOMPARE(row.authors, QString("Christos A. Ioannou; Julian Romero; Ann Smith"));
  QCOMPARE(row.year, QString("2013"));
}

void TestMetadataResolver::pagesWithZotero() {
  QList<PageContext> pages(2);
  pages[0].paperHash = "h1";
  pages[0].cleanTitle = "file name";
  pages[1].paperHash = "h2";
  pages[1].cleanTitle = "other file";
  MetadataResolver::attachZotero(pages, {{"h1", zoteroMeta()}});
  QVERIFY(pages[0].zotero.has_value());
  QVERIFY(!pages[1].zotero.has_value());
  QCOMPARE(MetadataResolver::paperLabel(pages[0]).footerForm(),
           QString("Ioannou et al. (2014)"));
  QCOMPARE(MetadataResolver::paperLabel(pages[1]).title, QString());
}

QTEST_GUILESS_MAIN(TestMetadataResolver)
#include "tst_metadataresolver.moc"
