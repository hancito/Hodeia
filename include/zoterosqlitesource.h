#pragma once

#include "zoterosource.h"

/// Reads a Zotero library from its zotero.sqlite.
///
/// Zotero keeps an exclusive lock on its database while it runs, and recent
/// changes may sit in zotero.sqlite-wal: load() copies both files to a
/// temporary folder and reads the copy, never the original.
class ZoteroSqliteSource : public ZoteroSource {
public:
  /// `dataDir`: Zotero's data folder (holds zotero.sqlite and storage/).
  /// `baseAttachmentPath`: base folder of relative linked files (prefs.js).
  explicit ZoteroSqliteSource(QString dataDir, QString baseAttachmentPath = {});

  std::expected<ZoteroSnapshot, QString> load() override;

  /// Highest `userdata` schema version tested.  A newer one is read anyway
  /// (with a warning); a failing query then names the version.
  static constexpr int kTestedUserdataVersion = 131;

private:
  QString m_dataDir;
  QString m_baseAttachmentPath;
};
