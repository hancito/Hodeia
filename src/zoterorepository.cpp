#include "zoterorepository.h"

#include "sqlitedb.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <Sqlite3/sqlite3.h>

namespace {

using Error = std::unexpected<QString>;

QString lastError(SqliteDb &db) {
  return QString::fromUtf8(sqlite3_errmsg(db.handle()));
}

QString lastError(const SqliteStatement &stmt) {
  return QString::fromUtf8(sqlite3_errmsg(sqlite3_db_handle(stmt.handle())));
}

/// Run a statement that returns no rows.
std::expected<void, QString> run(SqliteStatement &stmt) {
  if (auto r = stmt.exec(); !r)
    return Error(r.error());
  return {};
}

QString toJson(const QStringList &list) {
  return QString::fromUtf8(
      QJsonDocument(QJsonArray::fromStringList(list)).toJson(QJsonDocument::Compact));
}

QStringList fromJson(const QString &json) {
  QStringList list;
  for (const QJsonValue &v : QJsonDocument::fromJson(json.toUtf8()).array())
    list.append(v.toString());
  return list;
}

const char *const kItemColumns =
    "id, library_id, item_key, attachment_key, paper_hash, title, authors, "
    "year, journal, doi, item_type, tags, date_modified, status, removed_at";

ZoteroItemRow readItem(const SqliteStatement &s) {
  ZoteroItemRow row;
  row.id = s.columnInt(0);
  row.libraryId = s.columnInt(1);
  row.itemKey = s.columnText(2);
  row.attachmentKey = s.columnText(3);
  row.paperHash = s.columnText(4);
  row.meta.title = s.columnText(5);
  row.meta.authors = fromJson(s.columnText(6));
  row.meta.year = s.columnText(7);
  row.meta.journal = s.columnText(8);
  row.meta.doi = s.columnText(9);
  row.meta.itemType = s.columnText(10);
  row.meta.tags = fromJson(s.columnText(11));
  row.dateModified = s.columnText(12);
  row.status = zoteroStatusFromString(s.columnText(13)).value_or(ZoteroStatus::Pending);
  row.removedAt = s.columnDateTime(14);
  return row;
}

/// Run `body` inside a transaction: all of it, or nothing.
template <typename F>
std::expected<void, QString> inTransaction(SqliteDb &db, F body) {
  if (auto r = db.transaction(); !r)
    return r;
  if (auto r = body(); !r) {
    (void)db.rollback();
    return r;
  }
  return db.commit();
}

/// UPDATE ... WHERE id = ? for each id, `bind` setting the other values.
template <typename Bind>
std::expected<void, QString> updateIds(SqliteDb &db, const QString &sql,
                                       const QList<int> &ids, Bind bind) {
  return inTransaction(db, [&]() -> std::expected<void, QString> {
    SqliteStatement stmt = db.prepare(sql);
    if (!stmt.isValid())
      return Error(lastError(db));
    for (int id : ids) {
      stmt.resetAndClearBindings();
      const int idIndex = bind(stmt);
      (void)stmt.bind(idIndex, id);
      if (auto r = run(stmt); !r)
        return r;
    }
    return {};
  });
}

} // namespace

QString zoteroStatusName(ZoteroStatus status) {
  switch (status) {
  case ZoteroStatus::Pending:
    return QStringLiteral("pending");
  case ZoteroStatus::Ingested:
    return QStringLiteral("ingested");
  case ZoteroStatus::Ignored:
    return QStringLiteral("ignored");
  }
  return {};
}

std::optional<ZoteroStatus> zoteroStatusFromString(const QString &s) {
  for (ZoteroStatus st :
       {ZoteroStatus::Pending, ZoteroStatus::Ingested, ZoteroStatus::Ignored})
    if (zoteroStatusName(st) == s)
      return st;
  return std::nullopt;
}

ZoteroRepository::ZoteroRepository(SqliteDb &db) : m_db(db) {}

/* ── Followed collections ───────────────────────────────────────── */

QList<FollowedCollection> ZoteroRepository::followedCollections() const {
  QList<FollowedCollection> list;
  SqliteStatement stmt =
      m_db.prepare("SELECT library_id, collection_key, name "
                   "FROM ZoteroCollections ORDER BY library_id, name");
  while (true) {
    auto step = stmt.step();
    if (!step || !*step)
      break;
    list.append({stmt.columnInt(0), stmt.columnText(1), stmt.columnText(2)});
  }
  return list;
}

std::expected<void, QString> ZoteroRepository::setFollowedCollections(
    const QList<FollowedCollection> &collections) {
  return inTransaction(m_db, [&]() -> std::expected<void, QString> {
    if (auto r = m_db.exec("DELETE FROM ZoteroCollections"); !r)
      return r;
    SqliteStatement stmt = m_db.prepare(
        "INSERT INTO ZoteroCollections (library_id, collection_key, name) "
        "VALUES (?, ?, ?)");
    if (!stmt.isValid())
      return Error(lastError(m_db));
    for (const FollowedCollection &c : collections) {
      stmt.resetAndClearBindings();
      (void)stmt.bind(1, c.libraryId);
      (void)stmt.bind(2, c.key);
      (void)stmt.bind(3, c.name);
      if (auto r = run(stmt); !r)
        return r;
    }
    return {};
  });
}

/* ── Items ──────────────────────────────────────────────────────── */

QList<ZoteroItemRow> ZoteroRepository::items() const {
  QList<ZoteroItemRow> rows;
  SqliteStatement stmt = m_db.prepare(
      QString("SELECT %1 FROM ZoteroItems ORDER BY id").arg(kItemColumns));
  while (true) {
    auto step = stmt.step();
    if (!step || !*step)
      break;
    rows.append(readItem(stmt));
  }
  return rows;
}

std::expected<int, QString> ZoteroRepository::upsertItem(const ZoteroItemRow &row) {
  SqliteStatement stmt = m_db.prepare(
      "INSERT INTO ZoteroItems (library_id, item_key, attachment_key, "
      "paper_hash, title, authors, year, journal, doi, item_type, tags, "
      "date_modified, status) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT (library_id, attachment_key) DO UPDATE SET "
      "item_key = excluded.item_key, paper_hash = excluded.paper_hash, "
      "title = excluded.title, authors = excluded.authors, "
      "year = excluded.year, journal = excluded.journal, doi = excluded.doi, "
      "item_type = excluded.item_type, tags = excluded.tags, "
      "date_modified = excluded.date_modified, removed_at = NULL "
      "RETURNING id");
  if (!stmt.isValid())
    return Error(lastError(m_db));
  (void)stmt.bind(1, row.libraryId);
  (void)stmt.bind(2, row.itemKey);
  (void)stmt.bind(3, row.attachmentKey);
  (void)stmt.bind(4, row.paperHash);
  (void)stmt.bind(5, row.meta.title);
  (void)stmt.bind(6, toJson(row.meta.authors));
  (void)stmt.bind(7, row.meta.year);
  (void)stmt.bind(8, row.meta.journal);
  (void)stmt.bind(9, row.meta.doi);
  (void)stmt.bind(10, row.meta.itemType);
  (void)stmt.bind(11, toJson(row.meta.tags));
  (void)stmt.bind(12, row.dateModified);
  (void)stmt.bind(13, zoteroStatusName(row.status));
  auto step = stmt.step();
  if (!step)
    return Error(step.error());
  if (!*step) // RETURNING always yields the row
    return Error(lastError(stmt));
  return stmt.columnInt(0);
}

std::expected<void, QString> ZoteroRepository::setStatus(const QList<int> &ids,
                                                         ZoteroStatus status) {
  return updateIds(m_db, "UPDATE ZoteroItems SET status = ? WHERE id = ?", ids,
                   [&](SqliteStatement &s) {
                     (void)s.bind(1, zoteroStatusName(status));
                     return 2;
                   });
}

std::expected<void, QString> ZoteroRepository::markRemoved(const QList<int> &ids,
                                                           const QDateTime &when) {
  return updateIds(
      m_db,
      "UPDATE ZoteroItems SET removed_at = ? WHERE id = ? AND removed_at IS NULL",
      ids, [&](SqliteStatement &s) {
        (void)s.bind(1, when.toUTC());
        return 2;
      });
}

namespace {
// Rows of a hash in order of preference: still in Zotero, then the user's
// library (library 1 is always the user's own in Zotero).
const char *const kPreference = "removed_at IS NOT NULL, library_id, id";
} // namespace

QHash<QString, ZoteroMeta> ZoteroRepository::metaByHash() const {
  QHash<QString, ZoteroMeta> out;
  SqliteStatement stmt = m_db.prepare(
      QString("SELECT %1 FROM ZoteroItems ORDER BY paper_hash, %2")
          .arg(kItemColumns, kPreference));
  while (true) {
    auto step = stmt.step();
    if (!step || !*step)
      break;
    const ZoteroItemRow row = readItem(stmt);
    if (!out.contains(row.paperHash)) // the first is the preferred one
      out.insert(row.paperHash, row.meta);
  }
  return out;
}

std::optional<ZoteroItemRow>
ZoteroRepository::itemByHash(const QString &paperHash) const {
  SqliteStatement stmt = m_db.prepare(
      QString("SELECT %1 FROM ZoteroItems WHERE paper_hash = ? "
              "ORDER BY %2 LIMIT 1")
          .arg(kItemColumns, kPreference));
  (void)stmt.bind(1, paperHash);
  auto step = stmt.step();
  if (!step || !*step)
    return std::nullopt;
  return readItem(stmt);
}
