// Unit tests for SummaryFormat::parseJson: valid summaries and the repairs of
// the malformed JSON small models produce.

#include "summaryformat.h"

#include <QtTest>

class TestSummaryFormat : public QObject {
  Q_OBJECT

private slots:
  void valid();
  void codeFenceAndChatter();
  void trailingCommas();
  void strayQuotes();
  void strayQuoteBeforeComma();
  void truncated();
  void noJson();
  void unreadable_data();
  void unreadable();
};

void TestSummaryFormat::valid() {
  const PaperSummary s = SummaryFormat::parseJson(
      R"({"title":"T","authors":["A","B"],"year":1984,)"
      R"("coreFindingsAndSignificance":["x \"quoted\" y"]})");
  QCOMPARE(s.title, QString("T"));
  QCOMPARE(s.authors, QStringList({"A", "B"}));
  QCOMPARE(s.year, 1984);
  QCOMPARE(s.coreFindingsAndSignificance,
           QStringList({"x \"quoted\" y"}));
}

void TestSummaryFormat::codeFenceAndChatter() {
  const PaperSummary s = SummaryFormat::parseJson(
      "Here is the summary:\n```json\n{\"title\": \"T\"}\n```\nDone.");
  QCOMPARE(s.title, QString("T"));
}

void TestSummaryFormat::trailingCommas() {
  const PaperSummary s =
      SummaryFormat::parseJson(R"({"title":"T","topics":["a","b",],})");
  QCOMPARE(s.title, QString("T"));
  QCOMPARE(s.topics, QStringList({"a", "b"}));
}

// Real case (gemma-4-E2B, 2026-11-18): the model copied the notation w''
// from an OCR'd page as `vv"`, leaving raw quotes inside the strings.
void TestSummaryFormat::strayQuotes() {
  const QString raw = QStringLiteral(
      "{\n  \"title\": \"Equilibrium Unemployment as a Worker Discipline "
      "Device: Comment\",\n  \"authors\": [\n    \"CLIVE BULL\"\n  ],\n"
      "  \"year\": 1984,\n  \"coreFindingsAndSignificance\": [\n"
      "    \"the optimal trade involves vv\" = w = 0.\",\n"
      "    \"no private incentive (vv\" > 0) is challenged.\"\n  ],\n"
      "  \"criticalLimitations\": [\n"
      "    \"relies on the assumption that vv\" must be paid.\"\n  ],\n"
      "  \"citableSummary\": \"S\"\n}");
  const PaperSummary s = SummaryFormat::parseJson(raw);
  QCOMPARE(s.title,
           QString("Equilibrium Unemployment as a Worker Discipline Device: "
                   "Comment"));
  QCOMPARE(s.authors, QStringList({"CLIVE BULL"}));
  QCOMPARE(s.year, 1984);
  QCOMPARE(s.coreFindingsAndSignificance,
           QStringList({"the optimal trade involves vv\" = w = 0.",
                        "no private incentive (vv\" > 0) is challenged."}));
  QCOMPARE(s.criticalLimitations,
           QStringList({"relies on the assumption that vv\" must be paid."}));
  QCOMPARE(s.citableSummary, QString("S"));
}

// A raw quote followed by a comma inside running text is still text: the
// comma is not followed by another string.
void TestSummaryFormat::strayQuoteBeforeComma() {
  const PaperSummary s = SummaryFormat::parseJson(
      R"({"methodology":"the "efficiency wage", then a test","title":"T"})");
  QCOMPARE(s.methodology, QString("the \"efficiency wage\", then a test"));
  QCOMPARE(s.title, QString("T"));
}

void TestSummaryFormat::truncated() {
  const PaperSummary s = SummaryFormat::parseJson(
      R"({"title":"T","topics":["a","b)");
  QCOMPARE(s.title, QString("T"));
  QCOMPARE(s.topics, QStringList({"a", "b"}));
}

void TestSummaryFormat::noJson() {
  const PaperSummary s = SummaryFormat::parseJson("no json here");
  QVERIFY(s.title.isEmpty());
}

void TestSummaryFormat::unreadable_data() {
  QTest::addColumn<QString>("raw");
  QTest::newRow("empty") << "";
  QTest::newRow("plain text") << "I cannot summarize this paper.";
  QTest::newRow("empty object") << "{}";
  QTest::newRow("other fields") << R"({"answer":"42"})";
  QTest::newRow("beyond repair") << R"({"title": "T" "authors": ["A"]})";
}

// The summarizer treats these as failures: tryParseJson returns nothing,
// parseJson an empty summary (display of stored summaries).
void TestSummaryFormat::unreadable() {
  QFETCH(QString, raw);
  QVERIFY(!SummaryFormat::tryParseJson(raw).has_value());
  QVERIFY(SummaryFormat::parseJson(raw).title.isEmpty());
}

QTEST_GUILESS_MAIN(TestSummaryFormat)
#include "tst_summaryformat.moc"
