// Unit tests for MetadataResolver: how papers are named in citations.

#include "metadataresolver.h"

#include <QtTest>

class TestMetadataResolver : public QObject {
  Q_OBJECT

private slots:
  void compactAuthors_data();
  void compactAuthors();
  void paperLabel_data();
  void paperLabel();
  void forms();
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

QTEST_GUILESS_MAIN(TestMetadataResolver)
#include "tst_metadataresolver.moc"
