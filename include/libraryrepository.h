#pragma once

#include "summaryformat.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <expected>

class SqliteDb;

/// One paper of the summary list (Summaries, with its citation count).
struct SummaryRow {
  int id = 0;
  QString paperHash;
  QString title;
  QString authors;
  int pagesNum = 0;
  QString journal;
  QString year;
  QString topics;
  QString llmodel;
  QVariant summaryJson;
  QDateTime date;
  // Number of works that cite this paper (from Citations.cites_count, 0 if
  // the paper has no citation data yet).
  int citesCount = 0;
};

/// One answered query of the history (Queries).
struct QueryRow {
  int id = 0;
  QString queryText;
  QString response;
  QDateTime date;
  QString llmodel;
};

/// Citation data of one summarized paper (Citations).
struct CitationRow {
  QString paperHash;
  // Raw JSON blobs as stored in the Citations table (an array of works, or a
  // marker string when OpenAlex has no data).
  QString cites;   // works that cite this paper
  QString citedBy; // works this paper cited
  int citesCount = 0;
  int citedByCount = 0;
};

/// A paper present in the chunks table (ingested), summarized or not.
struct IndexedPaper {
  QString paperHash;
  QString cleanTitle; ///< summary title, else the file name
  QString citation;   ///< "Authors (year)" once summarized, else file name
  int pages = 0;      ///< highest page with text + 1
};

/// Result of a batch delete: how many items could not be (fully) deleted,
/// and the last database error.
struct BatchResult {
  bool prepareFailed = false; ///< nothing was attempted
  int failed = 0;
  QString lastError;
  bool ok() const { return !prepareFailed && failed == 0; }
};

/// SQL access to the library of an open project database: what the lists
/// show, what the workers save, what the user deletes.  No Qt Quick, no
/// models: usable from any thread holding the (FULLMUTEX) connection, and
/// headless (MCP server).
///
/// The OpenAlex client and the ingestion keep their own SQL for now.
class LibraryRepository {
public:
  explicit LibraryRepository(SqliteDb &db);

  /* ── Reads ── */

  /// Every summarized paper, in insertion order.
  QList<SummaryRow> summaries() const;
  /// The query history, oldest first.
  QList<QueryRow> queries() const;
  /// Citation data of the summarized papers, by hash.
  QList<CitationRow> citations() const;
  /// Every ingested paper (in the chunks table), by hash.
  QList<IndexedPaper> indexedPapers() const;
  /// Title of a summarized paper, empty if none.
  QString summaryTitle(const QString &paperHash) const;

  /* ── Writes ── */

  /// Store (or replace) the summary of a paper, then give its chunks the
  /// summary's title and "Authors (year)" citation, and register the paper
  /// for an OpenAlex lookup.  Fails only if the summary itself could not be
  /// stored; the chunk and citation updates are best effort (logged).
  std::expected<void, QString> saveSummary(const QString &paperHash,
                                           const PaperSummary &summary,
                                           const QString &rawJson,
                                           const QString &modelName);

  /// Add an answer to the query history.
  std::expected<void, QString> saveQuery(const QString &queryText,
                                         const QString &response,
                                         const QString &modelName);

  /// Delete papers: summary, chunks and citation data.
  BatchResult removePapers(const QStringList &paperHashes);
  /// Delete queries from the history.
  BatchResult removeQueries(const QList<int> &ids);

private:
  SqliteDb &m_db;
};
