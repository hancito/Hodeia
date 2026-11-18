// Unit tests for ZoteroLocator: reading prefs.js and finding Zotero's data
// folder (saved path, profile, defaults).

#include "zoterolocator.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {

void writeFile(const QString &path, const QByteArray &content) {
  QDir().mkpath(QFileInfo(path).path());
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(content);
}

/// A folder that looks like a Zotero data folder.
QString makeDataDir(const QTemporaryDir &root, const QString &name) {
  const QString dir = root.filePath(name);
  writeFile(dir + "/zotero.sqlite", "");
  return dir;
}

} // namespace

class TestZoteroLocator : public QObject {
  Q_OBJECT

private slots:
  void parsePrefs();
  void parsePrefsIgnoresUnusedDataDir();
  void readPrefsFromProfile();
  void findDataDirOrder();
  void findDataDirNone();
};

void TestZoteroLocator::parsePrefs() {
  const auto prefs = ZoteroLocator::parsePrefs(
      "// Mozilla User Preferences\n"
      "user_pref(\"extensions.zotero.dataDir\", \"C:\\\\Users\\\\me\\\\Zotero\");\n"
      "user_pref(\"extensions.zotero.useDataDir\", true);\n"
      "user_pref(\"extensions.zotero.baseAttachmentPath\", \"D:\\\\Papers\");\n"
      "user_pref(\"extensions.zotero.sync.server.username\", \"x\");\n");
  QCOMPARE(prefs.dataDir, QString("C:/Users/me/Zotero"));
  QCOMPARE(prefs.baseAttachmentPath, QString("D:/Papers"));
}

void TestZoteroLocator::parsePrefsIgnoresUnusedDataDir() {
  // Zotero keeps an old dataDir after the user goes back to the default.
  const auto prefs = ZoteroLocator::parsePrefs(
      "user_pref(\"extensions.zotero.dataDir\", \"/old/place\");\n"
      "user_pref(\"extensions.zotero.useDataDir\", false);\n");
  QVERIFY(prefs.dataDir.isEmpty());
  QVERIFY(ZoteroLocator::parsePrefs("").dataDir.isEmpty());
}

void TestZoteroLocator::readPrefsFromProfile() {
  QTemporaryDir root;
  writeFile(root.filePath("Profiles/abcd1234.default/prefs.js"),
            "user_pref(\"extensions.zotero.dataDir\", \"/data/Zotero\");\n"
            "user_pref(\"extensions.zotero.useDataDir\", true);\n");
  const auto prefs = ZoteroLocator::readPrefs({root.filePath("missing"),
                                               root.filePath("Profiles")});
  QCOMPARE(prefs.dataDir, QString("/data/Zotero"));
  QVERIFY(ZoteroLocator::readPrefs({root.filePath("missing")}).dataDir.isEmpty());
}

void TestZoteroLocator::findDataDirOrder() {
  QTemporaryDir root;
  const QString saved = makeDataDir(root, "saved");
  const QString fromPrefs = makeDataDir(root, "prefs");
  const QString byDefault = makeDataDir(root, "default");
  const ZoteroLocator::Prefs prefs{fromPrefs, {}};
  const QStringList defaults{root.filePath("nothing"), byDefault};

  QCOMPARE(ZoteroLocator::findDataDir(saved, prefs, defaults), saved);
  // A saved folder that no longer holds a database is skipped.
  QCOMPARE(ZoteroLocator::findDataDir(root.filePath("gone"), prefs, defaults),
           fromPrefs);
  QCOMPARE(ZoteroLocator::findDataDir({}, {}, defaults), byDefault);
}

void TestZoteroLocator::findDataDirNone() {
  QTemporaryDir root;
  QVERIFY(ZoteroLocator::findDataDir(root.path(), {}, {root.path()}).isEmpty());
  QVERIFY(!ZoteroLocator::isDataDir({}));
}

QTEST_GUILESS_MAIN(TestZoteroLocator)
#include "tst_zoterolocator.moc"
