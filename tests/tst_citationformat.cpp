// Unit tests for CitationFormat: reference tags → author-year citations and
// the Sources footer.

#include "citationformat.h"

#include <QJsonObject>
#include <QtTest>

namespace {

PageContext page(const QString &hash, int page0, const QString &citation,
                 const QString &title) {
  PageContext pc;
  pc.paperHash = hash;
  pc.page = page0;
  pc.citation = citation;
  pc.cleanTitle = title;
  return pc;
}

// [1] Walker p. 5, [2] Marley p. 2, [3] Walker p. 7
QList<PageContext> samplePages() {
  return {page("aaa", 4, "J. Walker, M. Ben-Akiva (2002)", "Hybrid Choice"),
          page("bbb", 1, "A. Marley (2002)", "Random Utility"),
          page("aaa", 6, "J. Walker, M. Ben-Akiva (2002)", "Hybrid Choice")};
}

} // namespace

class TestCitationFormat : public QObject {
  Q_OBJECT

private slots:
  void pageRange();
  void singleTag();
  void groupedTags();
  void commaList();
  void gluedTag();
  void unknownAndLinks();
  void sourcesCitedOnly();
  void sourcesAllWhenNoneCited();
  void sourcesEmpty();
  void sentence();
  void marks();
  void apaReference();
};

void TestCitationFormat::pageRange() {
  QCOMPARE(CitationFormat::pageRange({4}), QString("p. 4"));
  QCOMPARE(CitationFormat::pageRange({7, 2, 7, 5}), QString("pp. 2, 5, 7"));
}

void TestCitationFormat::singleTag() {
  QSet<int> cited;
  const QString out = CitationFormat::resolveReferenceTags(
      "Latent classes [2].", samplePages(), cited);
  QCOMPARE(out, QString("Latent classes (Marley, 2002, p. 2)."));
  QCOMPARE(cited, QSet<int>({1}));
}

void TestCitationFormat::groupedTags() {
  QSet<int> cited;
  const QString out = CitationFormat::resolveReferenceTags(
      "As shown [1][2][3].", samplePages(), cited);
  // Pages of the same paper merged, papers in citation order.
  QCOMPARE(out, QString("As shown (Walker & Ben-Akiva, 2002, pp. 5, 7; "
                        "Marley, 2002, p. 2)."));
  QCOMPARE(cited, QSet<int>({0, 1, 2}));
}

void TestCitationFormat::commaList() {
  QSet<int> cited;
  const QString out = CitationFormat::resolveReferenceTags(
      "See [1, 3].", samplePages(), cited);
  QCOMPARE(out, QString("See (Walker & Ben-Akiva, 2002, pp. 5, 7)."));
}

void TestCitationFormat::gluedTag() {
  QSet<int> cited;
  const QString out = CitationFormat::resolveReferenceTags(
      "absence[2]", samplePages(), cited);
  QCOMPARE(out, QString("absence (Marley, 2002, p. 2)"));
}

void TestCitationFormat::unknownAndLinks() {
  QSet<int> cited;
  const QString text = "Out of range [9], a link [1](http://x) stays.";
  QCOMPARE(CitationFormat::resolveReferenceTags(text, samplePages(), cited),
           text);
  QVERIFY(cited.isEmpty());
}

void TestCitationFormat::sourcesCitedOnly() {
  const QString s = CitationFormat::sourcesSection(samplePages(), {1});
  QCOMPARE(s, QString("\n\n---\n\n**Sources**\n\n"
                      "- **Marley (2002)** — *Random Utility* — p. 2"));
}

void TestCitationFormat::sourcesAllWhenNoneCited() {
  const QString s = CitationFormat::sourcesSection(samplePages(), {});
  QCOMPARE(s, QString("\n\n---\n\n**Sources**\n\n"
                      "- **Walker & Ben-Akiva (2002)** — *Hybrid Choice* — "
                      "pp. 5, 7\n"
                      "- **Marley (2002)** — *Random Utility* — p. 2"));
}

void TestCitationFormat::sourcesEmpty() {
  QVERIFY(CitationFormat::sourcesSection({}, {}).isEmpty());
}

void TestCitationFormat::sentence() {
  QCOMPARE(CitationFormat::sentence("Journal"), QString("Journal."));
  QCOMPARE(CitationFormat::sentence("J. Econ."), QString("J. Econ."));
  QCOMPARE(CitationFormat::sentence("Why? "), QString("Why?"));
  QCOMPARE(CitationFormat::sentence(""), QString(""));
}

void TestCitationFormat::marks() {
  const QString hash = "95f8ed1543981065d32ad0c4932dda45";
  QCOMPARE(CitationFormat::shortId(hash), QString("95f8ed154398"));
  QCOMPARE(CitationFormat::paperMark(hash), QString("[@95f8ed154398]"));
  QCOMPARE(CitationFormat::pageMark(hash, 12), QString("[@95f8ed154398:p12]"));
}

void TestCitationFormat::apaReference() {
  const QJsonObject work{
      {"display_name", "Sickness Absence"},
      {"authors", QString("Barmby, Tim") + QChar(0x1F) + "Ercolani, Marco" +
                      QChar(0x1F) + "Treble, John"},
      {"publication_year", 2002},
      {"journal", "The Economic Journal"},
      {"doi", "10.1111/1468-0297.00046"}};
  QCOMPARE(CitationFormat::apaReference(work, "Unknown"),
           QString("Barmby, Tim, Ercolani, Marco & Treble, John (2002). "
                   "Sickness Absence. The Economic Journal. "
                   "[10.1111/1468-0297.00046](https://doi.org/"
                   "10.1111/1468-0297.00046)"));
  // Nothing known but the missing title.
  QCOMPARE(CitationFormat::apaReference({}, "Unknown"), QString("Unknown"));
}

QTEST_GUILESS_MAIN(TestCitationFormat)
#include "tst_citationformat.moc"
