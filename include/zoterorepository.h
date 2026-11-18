#pragma once

#include "zoterosource.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <expected>
#include <optional>

class SqliteDb;

/// A collection of Zotero followed by the project (ZoteroCollections).
struct FollowedCollection {
  int libraryId = 0;
  QString key;
  QString name;
};

/// Where a Zotero PDF stands in the project.
enum class ZoteroStatus {
  Pending,  ///< proposed, not imported yet
  Ingested, ///< in the project (imported, or found already there by its hash)
  Ignored,  ///< refused by the user: not proposed again
};

QString zoteroStatusName(ZoteroStatus status);
std::optional<ZoteroStatus> zoteroStatusFromString(const QString &s);

/// A PDF of Zotero known to the project (ZoteroItems).
struct ZoteroItemRow {
  int id = 0; ///< 0 for a row not stored yet
  int libraryId = 0;
  QString itemKey;
  QString attachmentKey;
  QString paperHash;
  ZoteroMeta meta;
  QString dateModified; ///< Zotero's, to notice a change there
  ZoteroStatus status = ZoteroStatus::Pending;
  QDateTime removedAt;  ///< invalid while the PDF is still in Zotero
};

/// SQL access to the Zotero tables of an open project database (schema v2).
/// No Qt Quick: usable from any thread holding the (FULLMUTEX) connection.
class ZoteroRepository {
public:
  explicit ZoteroRepository(SqliteDb &db);

  /* ── Followed collections ── */

  QList<FollowedCollection> followedCollections() const;
  /// Replace the followed collections with `collections`, at once.
  std::expected<void, QString>
  setFollowedCollections(const QList<FollowedCollection> &collections);

  /* ── Items ── */

  /// Every known PDF, in insertion order.
  QList<ZoteroItemRow> items() const;

  /// Store a PDF, by (library, attachment key).  A new one gets `row.status`;
  /// a known one gets the new hash, metadata and date, keeps its status, and
  /// is no longer marked removed.  Returns the row id.
  std::expected<int, QString> upsertItem(const ZoteroItemRow &row);

  std::expected<void, QString> setStatus(const QList<int> &ids,
                                         ZoteroStatus status);
  /// Mark PDFs as gone from Zotero (or from the followed collections) at
  /// `when`; rows already marked keep their first date.
  std::expected<void, QString> markRemoved(const QList<int> &ids,
                                           const QDateTime &when);

  /// Zotero metadata of a paper of the project, for display: among the rows
  /// of that hash, those still in Zotero first, then the user's library
  /// (library 1); nullopt if the paper is not in ZoteroItems.
  std::optional<ZoteroItemRow> itemByHash(const QString &paperHash) const;

private:
  SqliteDb &m_db;
};
