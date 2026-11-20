// Unit tests for the OpenAlex client's pure helpers: the DOI lookup (Zotero's
// DOI first, the title search as fallback).

#include "openalex.h"

#include <QtTest>

class TestOpenAlex : public QObject {
  Q_OBJECT

private slots:
  void normalizeDoi_data();
  void normalizeDoi();
  void workIdOf();
};

void TestOpenAlex::normalizeDoi_data() {
  QTest::addColumn<QString>("input");
  QTest::addColumn<QString>("expected");
  QTest::newRow("plain") << "10.7717/peerj.4375" << "10.7717/peerj.4375";
  QTest::newRow("url") << "https://doi.org/10.7717/PeerJ.4375" << "10.7717/peerj.4375";
  QTest::newRow("dx url") << "http://dx.doi.org/10.2307/2231592" << "10.2307/2231592";
  QTest::newRow("doi: prefix") << " doi:10.2307/2231592 " << "10.2307/2231592";
  QTest::newRow("parentheses") << "10.1016/0022-0531(80)90056-3"
                               << "10.1016/0022-0531(80)90056-3";
  QTest::newRow("empty") << "" << "";
  QTest::newRow("not a DOI") << "ISBN 978-0-262-13466-8" << "";
  QTest::newRow("no suffix") << "10.1234/" << "";
}

void TestOpenAlex::normalizeDoi() {
  QFETCH(QString, input);
  QFETCH(QString, expected);
  QCOMPARE(OpenAlexClient::normalizeDoi(input), expected);
}

void TestOpenAlex::workIdOf() {
  QCOMPARE(OpenAlexClient::workIdOf(
               "{\"id\":\"https://openalex.org/W2741809807\",\"display_name\":\"OA\"}"),
           QString("W2741809807"));
  QVERIFY(OpenAlexClient::workIdOf("{\"error\":\"Not found\"}").isEmpty());
  QVERIFY(OpenAlexClient::workIdOf("not json").isEmpty());
}

QTEST_GUILESS_MAIN(TestOpenAlex)
#include "tst_openalex.moc"
