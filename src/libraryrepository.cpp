#include "libraryrepository.h"

#include "sqlitedb.h"

#include <QCoreApplication>
#include <QDebug>
#include <QSet>
#include <algorithm>

namespace {

QString prepareFailed() {
  return QCoreApplication::translate("LibraryRepository",
                                     "database error (prepare failed)");
}

} // namespace

LibraryRepository::LibraryRepository(SqliteDb &db) : m_db(db) {}

/* ── Reads ──────────────────────────────────────────────────────── */

namespace {

// LEFT JOIN so papers without citation data still appear (cites_count 0).
const char *const kSummarySelect =
    "SELECT s.id, s.paper_hash, s.title, s.authors, s.pages_num, s.journal, "
    "s.year, s.topics, s.llmodel, s.summary_json, s.date, "
    "COALESCE(c.cites_count, 0) "
    "FROM Summaries s LEFT JOIN Citations c ON c.paper_hash = s.paper_hash ";

QList<SummaryRow> readSummaries(SqliteStatement &stmt) {
  QList<SummaryRow> rows;
  while (true) {
    auto step = stmt.step();
    if (!step || !*step)
      break;
    SummaryRow row;
    row.id = stmt.columnInt(0);
    row.paperHash = stmt.columnText(1);
    row.title = stmt.columnText(2);
    row.authors = stmt.columnText(3);
    row.pagesNum = stmt.columnInt(4);
    row.journal = stmt.columnText(5);
    row.year = stmt.columnText(6);
    row.topics = stmt.columnText(7);
    row.llmodel = stmt.columnText(8);
    row.summaryJson = stmt.columnBlob(9);
    row.date = stmt.columnDateTime(10);
    row.citesCount = stmt.columnInt(11);
    rows.append(row);
  }
  return rows;
}

// Only summarized papers (joined to Summaries), matching the paper list.
const char *const kCitationSelect =
    "SELECT c.paper_hash, c.cites, c.cited_by, c.cites_count, "
    "c.cited_by_count FROM Citations c JOIN Summaries s "
    "ON s.paper_hash = c.paper_hash ";

QList<CitationRow> readCitations(SqliteStatement &stmt) {
  QList<CitationRow> rows;
  while (true) {
    auto step = stmt.step();
    if (!step || !*step)
      break;
    CitationRow row;
    row.paperHash = stmt.columnText(0);
    row.cites = stmt.columnText(1);
    row.citedBy = stmt.columnText(2);
    row.citesCount = stmt.columnInt(3);
    row.citedByCount = stmt.columnInt(4);
    rows.append(row);
  }
  return rows;
}

const char *const kIndexedSelect =
    "SELECT paper_hash, MAX(clean_title), MAX(citation), MAX(page) + 1 "
    "FROM chunks ";

QList<IndexedPaper> readIndexed(SqliteStatement &stmt) {
  QList<IndexedPaper> rows;
  while (true) {
    auto step = stmt.step();
    if (!step || !*step)
      break;
    rows.append({stmt.columnText(0), stmt.columnText(1), stmt.columnText(2),
                 stmt.columnInt(3)});
  }
  return rows;
}

/// The first row of `select` + `where` for `paperHash`, if any.
template <typename Row, typename Read>
std::optional<Row> oneRow(SqliteDb &db, const QString &sql,
                          const QString &paperHash, Read read) {
  SqliteStatement stmt = db.prepare(sql);
  if (!stmt.isValid() || !stmt.bind(1, paperHash))
    return std::nullopt;
  const QList<Row> rows = read(stmt);
  if (rows.isEmpty())
    return std::nullopt;
  return rows.first();
}

} // namespace

QList<SummaryRow> LibraryRepository::summaries() const {
  SqliteStatement stmt =
      m_db.prepare(QString(kSummarySelect) + "ORDER BY s.id");
  return readSummaries(stmt);
}

std::optional<SummaryRow>
LibraryRepository::summary(const QString &paperHash) const {
  return oneRow<SummaryRow>(m_db,
                            QString(kSummarySelect) + "WHERE s.paper_hash = ?",
                            paperHash, readSummaries);
}

QList<QueryRow> LibraryRepository::queries() const {
  QList<QueryRow> rows;
  SqliteStatement stmt = m_db.prepare(
      "SELECT id, query, response, date, llmodel FROM Queries ORDER BY id");
  while (true) {
    auto step = stmt.step();
    if (!step || !*step)
      break;
    QueryRow row;
    row.id = stmt.columnInt(0);
    row.queryText = stmt.columnText(1);
    row.response = stmt.columnText(2);
    row.date = stmt.columnDateTime(3);
    row.llmodel = stmt.columnText(4);
    rows.append(row);
  }
  return rows;
}

QList<CitationRow> LibraryRepository::citations() const {
  SqliteStatement stmt =
      m_db.prepare(QString(kCitationSelect) + "ORDER BY c.paper_hash");
  return readCitations(stmt);
}

std::optional<CitationRow>
LibraryRepository::citation(const QString &paperHash) const {
  return oneRow<CitationRow>(
      m_db, QString(kCitationSelect) + "WHERE c.paper_hash = ?", paperHash,
      readCitations);
}

QList<IndexedPaper> LibraryRepository::indexedPapers() const {
  SqliteStatement stmt = m_db.prepare(
      QString(kIndexedSelect) + "GROUP BY paper_hash ORDER BY paper_hash");
  return readIndexed(stmt);
}

std::optional<IndexedPaper>
LibraryRepository::indexedPaper(const QString &paperHash) const {
  // GROUP BY: no row (not an aggregate of nothing) for an unknown paper.
  return oneRow<IndexedPaper>(
      m_db, QString(kIndexedSelect) + "WHERE paper_hash = ? GROUP BY paper_hash",
      paperHash, readIndexed);
}

QStringList
LibraryRepository::paperHashesStartingWith(const QString &prefix) const {
  if (prefix.isEmpty())
    return {};
  SqliteStatement stmt = m_db.prepare(
      "SELECT paper_hash FROM Summaries "
      "WHERE substr(paper_hash, 1, length(?1)) = ?1 ORDER BY paper_hash");
  if (!stmt.isValid() || !stmt.bind(1, prefix))
    return {};
  QStringList hashes;
  while (true) {
    auto step = stmt.step();
    if (!step || !*step)
      break;
    hashes << stmt.columnText(0);
  }
  return hashes;
}

QString LibraryRepository::summaryTitle(const QString &paperHash) const {
  SqliteStatement stmt =
      m_db.prepare("SELECT title FROM Summaries WHERE paper_hash = ?");
  if (!stmt.isValid())
    return {};
  (void)stmt.bind(1, paperHash);
  auto step = stmt.step();
  if (!step || !*step)
    return {};
  return stmt.columnText(0).trimmed();
}

std::expected<QStringList, QString> LibraryRepository::paperHashes() const {
  SqliteStatement stmt = m_db.prepare("SELECT paper_hash FROM Summaries");
  if (!stmt.isValid())
    return std::unexpected(prepareFailed());
  QStringList hashes;
  while (true) {
    auto step = stmt.step();
    if (!step)
      return std::unexpected(step.error());
    if (!*step)
      break;
    hashes << stmt.columnText(0);
  }
  return hashes;
}

namespace {

/// Read the chunk columns (paper_hash, page, chunk_idx, chunk_text,
/// citation, clean_title, page_metadata[, distance]) of each row.
QList<ChunkSearchResult> readChunks(SqliteStatement &stmt, bool withDistance) {
  QList<ChunkSearchResult> results;
  while (true) {
    auto step = stmt.step();
    if (!step) {
      qCritical() << "[LibraryRepository] Reading chunks failed:"
                  << step.error();
      break;
    }
    if (!*step)
      break;
    ChunkSearchResult r;
    r.paperHash = stmt.columnText(0);
    r.page = stmt.columnInt(1);
    r.chunkIdx = stmt.columnInt(2);
    r.text = stmt.columnText(3);
    r.citation = stmt.columnText(4);
    r.cleanTitle = stmt.columnText(5);
    r.pageMetadata = stmt.columnText(6);
    if (withDistance)
      r.distance = float(stmt.columnDouble(7));
    results.append(r);
  }
  return results;
}

} // namespace

QList<ChunkSearchResult>
LibraryRepository::nearestChunks(const QByteArray &queryEmbedding, int k,
                                 const QStringList &paperHashes) const {
  if (queryEmbedding.isEmpty() || k <= 0)
    return {};
  // One KNN query per paper (vec0 filters on a metadata column with '='),
  // merged by distance.
  const QStringList filters =
      paperHashes.isEmpty() ? QStringList{QString()} : paperHashes;
  QList<ChunkSearchResult> results;
  for (const QString &hash : filters) {
    const QString sql =
        QString("SELECT paper_hash, page, chunk_idx, chunk_text, citation, "
                "clean_title, page_metadata, distance "
                "FROM chunks WHERE embedding MATCH vec_f32(?)%1 LIMIT %2")
            .arg(hash.isEmpty() ? QString() : QStringLiteral(" AND paper_hash = ?"))
            .arg(k);
    SqliteStatement stmt = m_db.prepare(sql);
    if (!stmt.isValid()) {
      qCritical() << "[LibraryRepository]" << prepareFailed();
      return {};
    }
    auto bound = stmt.bind(1, queryEmbedding);
    if (bound && !hash.isEmpty())
      bound = stmt.bind(2, hash);
    if (!bound) {
      qCritical() << "[LibraryRepository] Search failed:" << bound.error();
      return {};
    }
    results << readChunks(stmt, /*withDistance=*/true);
  }

  if (filters.size() > 1) {
    std::stable_sort(results.begin(), results.end(),
                     [](const ChunkSearchResult &a, const ChunkSearchResult &b) {
                       return a.distance < b.distance;
                     });
    if (results.size() > k)
      results.resize(k);
  }
  return results;
}

QList<ChunkSearchResult>
LibraryRepository::chunks(const QString &paperHash) const {
  if (paperHash.isEmpty())
    return {};
  SqliteStatement stmt = m_db.prepare(
      "SELECT paper_hash, page, chunk_idx, chunk_text, citation, clean_title, "
      "page_metadata FROM chunks WHERE paper_hash = ? ORDER BY page, chunk_idx");
  if (!stmt.isValid())
    return {};
  if (auto r = stmt.bind(1, paperHash); !r) {
    qCritical() << "[LibraryRepository] Reading chunks failed:" << r.error();
    return {};
  }
  return readChunks(stmt, /*withDistance=*/false);
}

/* ── Writes ─────────────────────────────────────────────────────── */

std::expected<void, QString>
LibraryRepository::addChunks(const QList<TextChunk> &chunks,
                             const QList<QByteArray> &embeddings) {
  if (chunks.size() != embeddings.size())
    return std::unexpected(QStringLiteral("%1 embeddings for %2 chunks")
                               .arg(embeddings.size())
                               .arg(chunks.size()));
  if (chunks.isEmpty())
    return {};
  SqliteStatement stmt = m_db.prepare(
      "INSERT INTO chunks (embedding, paper_hash, page, chunk_idx, chunk_text, "
      "citation, clean_title, page_metadata) "
      "VALUES (vec_f32(?), ?, ?, ?, ?, ?, ?, ?)");
  if (!stmt.isValid())
    return std::unexpected(prepareFailed());
  for (qsizetype i = 0; i < chunks.size(); ++i) {
    const TextChunk &c = chunks[i];
    std::expected<bool, QString> r = stmt.bind(1, embeddings[i]);
    if (r) r = stmt.bind(2, c.paperHash);
    if (r) r = stmt.bind(3, c.page);
    if (r) r = stmt.bind(4, c.chunkIdx);
    if (r) r = stmt.bind(5, c.text);
    if (r) r = stmt.bind(6, c.citation);
    if (r) r = stmt.bind(7, c.cleanTitle);
    if (r) r = stmt.bind(8, c.pageMetadata);
    if (r) r = stmt.exec();
    if (!r)
      return std::unexpected(r.error());
    stmt.resetAndClearBindings();
  }
  return {};
}

std::expected<void, QString>
LibraryRepository::addIndexedPaper(const SummaryRow &row,
                                   const QList<TextChunk> &chunks,
                                   const QList<QByteArray> &embeddings) {
  if (auto r = m_db.transaction(); !r)
    return std::unexpected(r.error());
  auto fail = [this](const QString &error) {
    (void)m_db.rollback();
    return std::unexpected(error);
  };

  SqliteStatement stmt = m_db.prepare(
      "INSERT INTO Summaries (paper_hash, title, authors, pages_num, journal, "
      "year, topics, llmodel, summary_json, date) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
  if (!stmt.isValid())
    return fail(prepareFailed());
  std::expected<bool, QString> r = stmt.bind(1, row.paperHash);
  if (r) r = stmt.bind(2, row.title);
  if (r) r = stmt.bind(3, row.authors);
  if (r) r = stmt.bind(4, row.pagesNum);
  if (r) r = stmt.bind(5, row.journal);
  if (r) r = stmt.bind(6, row.year);
  if (r) r = stmt.bind(7, row.topics);
  if (r) r = stmt.bind(8, row.llmodel);
  if (r) r = stmt.bind(9, row.summaryJson.toByteArray());
  if (r) r = stmt.bind(10, row.date);
  if (r) r = stmt.exec();
  if (!r)
    return fail(r.error());

  if (auto c = addChunks(chunks, embeddings); !c)
    return fail(c.error());
  if (auto c = m_db.commit(); !c)
    return fail(c.error());
  return {};
}

std::expected<void, QString>
LibraryRepository::saveSummary(const QString &paperHash,
                               const PaperSummary &summary,
                               const QString &rawJson,
                               const QString &modelName) {
  SqliteStatement stmt =
      m_db.prepare("INSERT INTO Summaries "
                   "(paper_hash, title, authors, pages_num, journal, year, "
                   "topics, llmodel, summary_json, date) "
                   "VALUES (?, ?, ?, 0, ?, ?, ?, ?, ?, ?) "
                   "ON CONFLICT(paper_hash) DO UPDATE SET "
                   "title = excluded.title, "
                   "authors = excluded.authors, "
                   "journal = excluded.journal, "
                   "year = excluded.year, "
                   "topics = excluded.topics, "
                   "llmodel = excluded.llmodel, "
                   "summary_json = excluded.summary_json, "
                   "date = excluded.date");
  if (!stmt.isValid()) {
    qCritical() << "[LibraryRepository] Failed to prepare summary upsert";
    return std::unexpected(prepareFailed());
  }
  (void)stmt.bind(1, paperHash);
  (void)stmt.bind(2, summary.title);
  (void)stmt.bind(3, summary.authors.join("; "));
  (void)stmt.bind(4, summary.journal);
  (void)stmt.bind(5, QString::number(summary.year));
  (void)stmt.bind(6, summary.topics.join(", "));
  (void)stmt.bind(7, modelName);
  (void)stmt.bind(8, rawJson.toUtf8());
  (void)stmt.bind(9, QDateTime::currentDateTimeUtc());
  if (auto r = stmt.exec(); !r) {
    qCritical() << "[LibraryRepository] Failed to save summary for"
                << paperHash << ":" << r.error();
    return std::unexpected(r.error());
  }

  // ── Chunk metadata (clean_title + citation) used to label the paper ──
  const QString citation =
      (summary.authors.join(", ") + " (" + QString::number(summary.year) + ")")
          .trimmed();
  SqliteStatement chunkStmt = m_db.prepare(
      "UPDATE chunks SET clean_title = ?, citation = ? WHERE paper_hash = ?");
  if (chunkStmt.isValid()) {
    (void)chunkStmt.bind(1, summary.title);
    (void)chunkStmt.bind(2, citation);
    (void)chunkStmt.bind(3, paperHash);
    if (auto r = chunkStmt.exec(); !r)
      qWarning() << "[LibraryRepository] Failed to update chunks metadata for"
                 << paperHash << ":" << r.error();
  } else {
    qWarning() << "[LibraryRepository] Failed to prepare chunks update for"
               << paperHash;
  }

  // ── Register in Citations table (OpenAlex will fill cites/cited_by) ──
  SqliteStatement refStmt =
      m_db.prepare("INSERT INTO Citations (paper_hash, clean_title, work_id, "
                   "cites, cited_by) VALUES (?, ?, '', '', '') "
                   "ON CONFLICT(paper_hash) DO UPDATE SET clean_title = "
                   "excluded.clean_title, "
                   // Re-summarised: the metadata may have improved, so give
                   // an unmatched paper another OpenAlex lookup right away.
                   "lookup_status = CASE WHEN work_id = '' THEN 'pending' "
                   "ELSE lookup_status END");
  if (refStmt.isValid()) {
    (void)refStmt.bind(1, paperHash);
    (void)refStmt.bind(2, summary.title);
    (void)refStmt.exec();
  }

  qDebug() << "[LibraryRepository] Saved summary for" << paperHash
           << "— title:" << summary.title;
  return {};
}

std::expected<void, QString>
LibraryRepository::saveQuery(const QString &queryText,
                             const QString &response,
                             const QString &modelName) {
  SqliteStatement stmt =
      m_db.prepare("INSERT INTO Queries (query, response, llmodel, date) "
                   "VALUES (?, ?, ?, ?)");
  if (!stmt.isValid()) {
    qCritical() << "[LibraryRepository] Failed to prepare query insert";
    return std::unexpected(prepareFailed());
  }
  (void)stmt.bind(1, queryText);
  (void)stmt.bind(2, response);
  (void)stmt.bind(3, modelName);
  (void)stmt.bind(4, QDateTime::currentDateTimeUtc());
  if (auto r = stmt.exec(); !r) {
    qCritical() << "[LibraryRepository] Failed to save query:" << r.error();
    return std::unexpected(r.error());
  }
  qDebug() << "[LibraryRepository] Saved query response (" << response.length()
           << "chars)";
  return {};
}

BatchResult LibraryRepository::removePapers(const QStringList &paperHashes) {
  BatchResult result;
  if (paperHashes.isEmpty())
    return result;
  QSet<QString> failed; // papers with at least one failed delete

  SqliteStatement stmt =
      m_db.prepare("DELETE FROM Summaries WHERE paper_hash = ?");
  if (!stmt.isValid()) {
    result.prepareFailed = true;
    return result;
  }
  for (const QString &hash : paperHashes) {
    (void)stmt.bind(1, hash);
    if (auto r = stmt.exec(); !r) {
      qWarning() << "[LibraryRepository] Delete summary failed for" << hash
                 << ":" << r.error();
      failed.insert(hash);
      result.lastError = r.error();
    }
    stmt.resetAndClearBindings();
  }

  // Associated vector chunks
  SqliteStatement chunkStmt =
      m_db.prepare("DELETE FROM chunks WHERE paper_hash = ?");
  if (chunkStmt.isValid()) {
    for (const QString &hash : paperHashes) {
      (void)chunkStmt.bind(1, hash);
      if (auto r = chunkStmt.exec(); !r) {
        qWarning() << "[LibraryRepository] Delete chunks failed for" << hash
                   << ":" << r.error();
        failed.insert(hash);
        result.lastError = r.error();
      }
      chunkStmt.resetAndClearBindings();
    }
  }

  // Associated citation data (best effort)
  SqliteStatement refStmt =
      m_db.prepare("DELETE FROM Citations WHERE paper_hash = ?");
  if (refStmt.isValid()) {
    for (const QString &hash : paperHashes) {
      (void)refStmt.bind(1, hash);
      (void)refStmt.exec();
      refStmt.resetAndClearBindings();
    }
  }

  result.failed = int(failed.size());
  return result;
}

BatchResult LibraryRepository::removeQueries(const QList<int> &ids) {
  BatchResult result;
  if (ids.isEmpty())
    return result;
  SqliteStatement stmt = m_db.prepare("DELETE FROM Queries WHERE id = ?");
  if (!stmt.isValid()) {
    qCritical() << "[LibraryRepository] Failed to prepare query delete";
    result.prepareFailed = true;
    return result;
  }
  for (int id : ids) {
    (void)stmt.bind(1, id);
    if (auto r = stmt.exec(); !r) {
      qWarning() << "[LibraryRepository] Delete query failed for id" << id
                 << ":" << r.error();
      ++result.failed;
      result.lastError = r.error();
    }
    stmt.resetAndClearBindings();
  }
  return result;
}
