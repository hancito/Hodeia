#pragma once

#include <QString>
#include <QStringList>

/// Where Zotero keeps its data.  Pure lookups: nothing is written, and
/// Zotero's own files are only read.
namespace ZoteroLocator {

/// The two settings of a Zotero profile (prefs.js) that HodeIA needs.
struct Prefs {
  QString dataDir;            ///< custom data folder; empty if the default is used
  QString baseAttachmentPath; ///< base of relative linked files; may be empty
};

/// Parse the content of a prefs.js.  `dataDir` is kept only when
/// `extensions.zotero.useDataDir` is true, as Zotero does.
Prefs parsePrefs(const QString &prefsJs);

/// Folders holding Zotero profiles (each with a prefs.js), for this OS.
QStringList profileRoots();

/// Settings of the first profile found under `roots` (see profileRoots()).
Prefs readPrefs(const QStringList &roots = profileRoots());

/// Default data folders for this OS (~/Zotero, ...).
QStringList defaultDataDirs();

/// True if `dir` holds a zotero.sqlite.
bool isDataDir(const QString &dir);

/// The data folder: `savedDir` if it is still valid, else the one declared
/// in the profile, else a default one; empty if none holds a zotero.sqlite
/// (the UI then asks the user for the folder).
QString findDataDir(const QString &savedDir, const Prefs &prefs = readPrefs(),
                    const QStringList &defaults = defaultDataDirs());

} // namespace ZoteroLocator
