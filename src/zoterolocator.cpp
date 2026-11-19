#include "zoterolocator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QRegularExpression>
#include <QTimeZone>

namespace ZoteroLocator {

namespace {

/// Value of a JavaScript string literal (without its quotes).
QString unescapeJs(const QString &s) {
  QString out;
  out.reserve(s.size());
  for (qsizetype i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      const QChar c = s[++i];
      out += c == 'n' ? QChar('\n') : c == 't' ? QChar('\t') : c;
    } else {
      out += s[i];
    }
  }
  return out;
}

QString home() { return QDir::homePath(); }

} // namespace

Prefs parsePrefs(const QString &prefsJs) {
  // user_pref("extensions.zotero.dataDir", "C:\\Users\\me\\Zotero");
  // user_pref("extensions.zotero.useDataDir", true);
  static const QRegularExpression pref(
      R"re(user_pref\(\s*"([^"]+)"\s*,\s*("((?:[^"\\]|\\.)*)"|true|false)\s*\))re");
  QString dataDir, base;
  bool useDataDir = false;
  for (auto it = pref.globalMatch(prefsJs); it.hasNext();) {
    const auto m = it.next();
    const QString name = m.captured(1);
    if (name == "extensions.zotero.dataDir")
      dataDir = unescapeJs(m.captured(3));
    else if (name == "extensions.zotero.useDataDir")
      useDataDir = m.captured(2) == "true";
    else if (name == "extensions.zotero.baseAttachmentPath")
      base = unescapeJs(m.captured(3));
  }
  return {useDataDir ? QDir::fromNativeSeparators(dataDir) : QString(),
          QDir::fromNativeSeparators(base)};
}

QStringList profileRoots() {
#if defined(Q_OS_WIN)
  return {qEnvironmentVariable("APPDATA") + "/Zotero/Zotero/Profiles"};
#elif defined(Q_OS_MACOS)
  return {home() + "/Library/Application Support/Zotero/Profiles"};
#else
  return {home() + "/.zotero/zotero",
          home() + "/.var/app/org.zotero.Zotero/.zotero/zotero"}; // Flatpak
#endif
}

Prefs readPrefs(const QStringList &roots) {
  for (const QString &root : roots) {
    const QDir dir(root);
    for (const QString &profile :
         dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
      QFile file(dir.filePath(profile + "/prefs.js"));
      if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        return parsePrefs(QString::fromUtf8(file.readAll()));
    }
  }
  return {};
}

QStringList defaultDataDirs() {
  QStringList dirs{home() + "/Zotero"};
#if defined(Q_OS_LINUX)
  dirs << home() + "/.var/app/org.zotero.Zotero/Zotero" // Flatpak
       << home() + "/snap/zotero-snap/common/Zotero";   // Snap
#endif
  return dirs;
}

bool isDataDir(const QString &dir) {
  return !dir.isEmpty() && QFileInfo(dir + "/zotero.sqlite").isFile();
}

QString dbSignature(const QString &dataDir) {
  if (!isDataDir(dataDir))
    return {};
  QString signature;
  for (const char *suffix : {"", "-wal"}) {
    const QFileInfo info(dataDir + "/zotero.sqlite" + suffix);
    signature += info.exists() ? QString("%1@%2;").arg(info.size()).arg(
                                     info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch())
                               : QStringLiteral("-;");
  }
  return signature;
}

QString findDataDir(const QString &savedDir, const Prefs &prefs,
                    const QStringList &defaults) {
  if (isDataDir(savedDir))
    return QDir::cleanPath(savedDir);
  if (isDataDir(prefs.dataDir))
    return QDir::cleanPath(prefs.dataDir);
  for (const QString &dir : defaults)
    if (isDataDir(dir))
      return QDir::cleanPath(dir);
  return {};
}

} // namespace ZoteroLocator
