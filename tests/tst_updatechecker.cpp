// Unit tests for UpdateChecker: version ordering and the choice of the
// release to offer in a GitHub /releases answer (no network).

#include "updatechecker.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

namespace {

QJsonObject release(const QString &tag, bool prerelease = false,
                    bool draft = false) {
  return QJsonObject{{"tag_name", tag},
                     {"prerelease", prerelease},
                     {"draft", draft},
                     {"html_url", "https://example.org/" + tag},
                     {"body", "notes " + tag}};
}

} // namespace

class TestUpdateChecker : public QObject {
  Q_OBJECT

private slots:
  void compare_data();
  void compare();
  void preRelease();
  void stableBuildIgnoresPreReleases();
  void preReleaseBuildSeesPreReleases();
  void skipsDraftsAndJunk();
  void noneNewer();
};

void TestUpdateChecker::compare_data() {
  QTest::addColumn<QString>("a");
  QTest::addColumn<QString>("b");
  QTest::addColumn<int>("sign");
  QTest::newRow("equal") << "0.99rc1" << "v0.99rc1" << 0;
  QTest::newRow("padded") << "1.0" << "1.0.0" << 0;
  QTest::newRow("rc<final") << "0.99rc1" << "0.99" << -1;
  QTest::newRow("rc1<rc2") << "0.99rc1" << "0.99rc2" << -1;
  QTest::newRow("beta<rc") << "0.99beta3" << "0.99rc1" << -1;
  QTest::newRow("alpha<beta") << "1.0alpha" << "1.0beta" << -1;
  QTest::newRow("final<patch") << "0.99" << "0.99.1" << -1;
  QTest::newRow("numeric") << "0.9" << "0.10" << -1;
  QTest::newRow("major") << "0.99" << "1.0rc1" << -1;
  QTest::newRow("invalid<valid") << "nightly" << "0.1" << -1;
}

void TestUpdateChecker::compare() {
  QFETCH(QString, a);
  QFETCH(QString, b);
  QFETCH(int, sign);
  const int ab = UpdateChecker::compareVersions(a, b);
  const int ba = UpdateChecker::compareVersions(b, a);
  QCOMPARE((ab > 0) - (ab < 0), sign);
  QCOMPARE((ba > 0) - (ba < 0), -sign);
}

void TestUpdateChecker::preRelease() {
  QVERIFY(UpdateChecker::isPreRelease("0.99rc1"));
  QVERIFY(UpdateChecker::isPreRelease("v1.0beta"));
  QVERIFY(!UpdateChecker::isPreRelease("1.0"));
  QVERIFY(!UpdateChecker::isPreRelease("garbage"));
}

void TestUpdateChecker::stableBuildIgnoresPreReleases() {
  const QJsonArray releases{release("v1.1rc1", true), release("v1.0.1"),
                            release("v1.0")};
  const auto r = UpdateChecker::newestRelease(releases, "1.0");
  QCOMPARE(r.version, QString("1.0.1"));
  QCOMPARE(r.url, QString("https://example.org/v1.0.1"));
  QCOMPARE(r.notes, QString("notes v1.0.1"));
}

void TestUpdateChecker::preReleaseBuildSeesPreReleases() {
  const QJsonArray releases{release("v0.99rc2", true), release("v0.99rc1", true)};
  QCOMPARE(UpdateChecker::newestRelease(releases, "0.99rc1").version,
           QString("0.99rc2"));
  // The final release wins over its candidates.
  const QJsonArray withFinal{release("v0.99rc2", true), release("v0.99")};
  QCOMPARE(UpdateChecker::newestRelease(withFinal, "0.99rc1").version,
           QString("0.99"));
}

void TestUpdateChecker::skipsDraftsAndJunk() {
  const QJsonArray releases{release("v2.0", false, true), release("nightly"),
                            release("v1.2")};
  QCOMPARE(UpdateChecker::newestRelease(releases, "1.0").version,
           QString("1.2"));
}

void TestUpdateChecker::noneNewer() {
  const QJsonArray releases{release("v0.99rc1", true), release("v0.98")};
  QVERIFY(UpdateChecker::newestRelease(releases, "0.99rc1").version.isEmpty());
  QVERIFY(UpdateChecker::newestRelease({}, "0.99rc1").version.isEmpty());
}

QTEST_GUILESS_MAIN(TestUpdateChecker)
#include "tst_updatechecker.moc"
