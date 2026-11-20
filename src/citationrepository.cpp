#include "citationrepository.h"

#include "metadataresolver.h"
#include "sqlitedb.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <Sqlite3/sqlite3.h>

namespace {

using Error = std::unexpected<QString>;

QString lastError(SqliteDb &db) {
  return QString::fromUtf8(sqlite3_errmsg(db.handle()));
}

} // namespace

CitationRepository::CitationRepository(SqliteDb &db) : m_db(db) {}

std::expected<QList<PendingCitation>, QString>
CitationRepository::pending(int retryDays) const {
  // Lookup status: pending | matched | not_found (searched without a DOI)
  // | not_found_doi (the DOI was tried too).
  SqliteStatement stmt = m_db.prepare(
      QString("SELECT c.paper_hash, c.work_id, c.cites = '', c.cited_by = '', "
              "COALESCE(NULLIF(s.title, ''), c.clean_title), s.authors, "
              "s.year, s.journal, COALESCE(z.doi, ''), "
              "COALESCE(s.summary_json, '') <> '', "
              "COALESCE(z.title, ''), COALESCE(z.authors, '[]'), "
              "COALESCE(z.year, ''), COALESCE(z.journal, '') "
              "FROM (SELECT Citations.*, "
              "  (SELECT zz.id FROM ZoteroItems zz WHERE zz.paper_hash = Citations.paper_hash "
              "   ORDER BY zz.doi = '', zz.removed_at IS NOT NULL, zz.id LIMIT 1) AS zid "
              "  FROM Citations) c "
              "LEFT JOIN ZoteroItems z ON z.id = c.zid "
              "LEFT JOIN Summaries s ON s.paper_hash = c.paper_hash "
              "WHERE (c.cites = '' OR c.cited_by = '') "
              "AND NOT (c.lookup_at > datetime('now', '-%1 days') "
              "  AND (c.lookup_status = 'not_found_doi' "
              "    OR (c.lookup_status = 'not_found' AND COALESCE(z.doi, '') = '')))")
          .arg(retryDays));
  if (!stmt.isValid())
    return Error(lastError(m_db));

  QList<PendingCitation> rows;
  while (true) {
    auto step = stmt.step();
    if (!step)
      return Error(step.error());
    if (!*step)
      break;
    PendingCitation p;
    p.paperHash = stmt.columnText(0);
    p.workId = stmt.columnText(1).trimmed();
    p.needCites = stmt.columnInt(2) != 0;
    p.needCitedBy = stmt.columnInt(3) != 0;
    p.title = stmt.columnText(4).trimmed();
    for (const QString &a : stmt.columnText(5).split(';', Qt::SkipEmptyParts))
      if (!a.trimmed().isEmpty())
        p.authors << a.trimmed();
    p.year = stmt.columnText(6).trimmed().toInt();
    p.journal = stmt.columnText(7).trimmed();
    p.doi = stmt.columnText(8);
    // Not summarized: the title is a file name and there are no authors —
    // Zotero's metadata is better for the search (when the DOI fails).
    const bool summarized = stmt.columnInt(9) != 0;
    const QString zoteroTitle = stmt.columnText(10).trimmed();
    if (!summarized && !zoteroTitle.isEmpty()) {
      p.title = zoteroTitle;
      p.authors.clear();
      for (const QJsonValue &a :
           QJsonDocument::fromJson(stmt.columnText(11).toUtf8()).array())
        p.authors << MetadataResolver::displayName(a.toString());
      p.year = stmt.columnText(12).trimmed().toInt();
      p.journal = stmt.columnText(13).trimmed();
    }
    rows << p;
  }
  return rows;
}

std::expected<int, QString> CitationRepository::registerDoiPapers() {
  if (auto r = m_db.exec(
          "INSERT INTO Citations (paper_hash, clean_title) "
          "SELECT z.paper_hash, MAX(z.title) FROM ZoteroItems z "
          "WHERE z.doi <> '' "
          "AND EXISTS (SELECT 1 FROM Summaries s WHERE s.paper_hash = z.paper_hash) "
          "AND NOT EXISTS (SELECT 1 FROM Citations c WHERE c.paper_hash = z.paper_hash) "
          "GROUP BY z.paper_hash");
      !r)
    return Error(r.error());
  return int(sqlite3_changes(m_db.handle()));
}

std::expected<void, QString>
CitationRepository::saveWorkId(const QString &paperHash, const QString &workId) {
  SqliteStatement stmt = m_db.prepare(
      "UPDATE Citations SET work_id = ?, lookup_status = 'matched', "
      "lookup_at = CURRENT_TIMESTAMP WHERE paper_hash = ?");
  if (!stmt.isValid())
    return Error(lastError(m_db));
  (void)stmt.bind(1, workId);
  (void)stmt.bind(2, paperHash);
  if (auto r = stmt.exec(); !r)
    return Error(r.error());
  return {};
}

std::expected<void, QString>
CitationRepository::markNotFound(const QString &paperHash, bool doiTried) {
  SqliteStatement stmt = m_db.prepare("UPDATE Citations SET lookup_status = ?, "
                                      "lookup_at = CURRENT_TIMESTAMP WHERE paper_hash = ?");
  if (!stmt.isValid())
    return Error(lastError(m_db));
  (void)stmt.bind(1, doiTried ? QStringLiteral("not_found_doi")
                              : QStringLiteral("not_found"));
  (void)stmt.bind(2, paperHash);
  if (auto r = stmt.exec(); !r)
    return Error(r.error());
  return {};
}

std::expected<void, QString>
CitationRepository::saveList(const QString &paperHash, CitationList list,
                             const QString &itemsJson, int total) {
  SqliteStatement stmt = m_db.prepare(
      list == CitationList::Cites
          ? "UPDATE Citations SET cites = ?, cites_count = ? WHERE paper_hash = ?"
          : "UPDATE Citations SET cited_by = ?, cited_by_count = ? WHERE paper_hash = ?");
  if (!stmt.isValid())
    return Error(lastError(m_db));
  (void)stmt.bind(1, itemsJson);
  (void)stmt.bind(2, total);
  (void)stmt.bind(3, paperHash);
  if (auto r = stmt.exec(); !r)
    return Error(r.error());
  return {};
}
