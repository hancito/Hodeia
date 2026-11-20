#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <expected>

class SqliteDb;

/// A paper whose citation lists are still to be fetched from OpenAlex.
struct PendingCitation {
  QString paperHash;
  QString workId; ///< OpenAlex work, empty while not matched
  bool needCites = true;
  bool needCitedBy = true;
  // Metadata to find the work: the summary's (LLM-extracted), or Zotero's
  // for a paper not summarized yet (its title is then a file name).
  QString title;
  QStringList authors;
  int year = 0;
  QString journal;
  QString doi; ///< Zotero's, as stored (not normalised); empty if none
};

/// The two citation lists of a paper.
enum class CitationList {
  Cites,   ///< Citations.cites / cites_count
  CitedBy, ///< Citations.cited_by / cited_by_count
};

/// SQL access to the OpenAlex state of the Citations table, for
/// OpenAlexClient.  The rows themselves are created and deleted with the
/// papers by LibraryRepository.  No Qt Quick: usable from any thread holding
/// the (FULLMUTEX) connection.
class CitationRepository {
public:
  explicit CitationRepository(SqliteDb &db);

  /// Papers missing at least one list, skipping those OpenAlex could not
  /// match less than `retryDays` ago (unless Zotero has since given a DOI
  /// to try).  The DOI is that of the paper's Zotero row: one with a DOI,
  /// still in Zotero, first.
  std::expected<QList<PendingCitation>, QString> pending(int retryDays) const;

  /// Give a Citations row to the project's papers (a Summaries row is made
  /// at import) that have a DOI in Zotero and none yet, with Zotero's title
  /// until a summary replaces it.  Returns how many were added.
  std::expected<int, QString> registerDoiPapers();

  /// The paper is the OpenAlex work `workId`.
  std::expected<void, QString> saveWorkId(const QString &paperHash,
                                          const QString &workId);
  /// OpenAlex has no work for the paper (`doiTried`: its DOI was tried too,
  /// so nothing new to try before the retry delay).
  std::expected<void, QString> markNotFound(const QString &paperHash,
                                            bool doiTried);
  /// Store one list (a JSON array of works) and OpenAlex's total for it.
  std::expected<void, QString> saveList(const QString &paperHash,
                                        CitationList list,
                                        const QString &itemsJson, int total);

private:
  SqliteDb &m_db;
};
