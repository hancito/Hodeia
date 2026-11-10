#pragma once

#include <QJsonArray>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThread>
#include <functional>
#include <optional>

class ConfigManager;
class DatabaseManager;
class QNetworkAccessManager;
class QNetworkReply;
class QUrlQuery;

/// Independent background service that fetches citation data from the
/// OpenAlex API and stores it in the Citations table.
///
/// For each paper it:
///   1. Finds the OpenAlex work by a 3-tier search — exact title, then
///      semantic, then author — disambiguated against the local metadata
///      (work_id).  Skipped when the work_id is already known.
///   2. Fetches the works it cites (its references)  -> Citations.cites
///   3. Fetches the works that cite it (citing papers) -> Citations.cited_by
///
/// Threading model:
///   * The client lives in its own QThread and does all the network work
///     there.  Papers are processed STRICTLY ONE AT A TIME from m_queue.
///     Within a paper, the two citation lists (cites / cited_by) are fetched
///     IN PARALLEL, each with its own buffer; the paper is finished when both
///     lists are done.
///   * It never touches SQLite from its own thread.  Every read (scan) and
///     write (work_id, lists, lookup status) is posted to the main thread
///     with m_database as context, where DatabaseManager owns the
///     connection.  Writes carry the DB path they were scanned from and are
///     dropped if the project changed in between.
///   * DatabaseManager::databaseSwitched() -> onDatabaseSwitched(): the queue
///     is flushed, the in-flight request aborted and the new DB rescanned.
///     m_generation invalidates any late reply / retry timer.
///
/// Triggered by:
///   - SummaryController::summaryFinished()  (after a summarization batch)
///   - checkEmptyReferences() on app startup (catch-up scan)
///   - a project switch (onDatabaseSwitched)
class OpenAlexClient : public QObject {
  Q_OBJECT

public:
  explicit OpenAlexClient(ConfigManager *config, DatabaseManager *database,
                          QObject *parent = nullptr);
  ~OpenAlexClient() override;

  // Local metadata used to disambiguate OpenAlex candidates.
  struct PaperMeta {
    QStringList authors;
    int year = 0;
    QString journal;
    QString cleanTitle;
  };

  /// Score each candidate against the local metadata and return the index of
  /// the best match, or -1 if none is a confident match.  Ranked by title,
  /// then authors, then year, then journal.  Pure function (no network, no
  /// DB) — public and static so it can be exercised in isolation.
  /// If @p exactTitleRejected is given, it is set to true when a candidate
  /// with the exact same (normalised) title was rejected by the author gate —
  /// the caller uses it to stop the search cascade early.
  static int bestCandidate(const QJsonArray &results, const PaperMeta &meta,
                           bool *exactTitleRejected = nullptr);

  /// Scan the Citations table for rows missing citation data and queue them.
  /// Safe to call from any thread (the DB read runs on the main thread).
  Q_INVOKABLE void checkEmptyReferences();

public slots:
  /// The active project DB changed: drop the queue, abort the in-flight
  /// request and rescan the new DB.
  void onDatabaseSwitched();

signals:
  /// Emitted (from the main thread) once a paper's lists are saved.
  void referencesSaved(const QString &paperHash);

private:
  // Which citation list we are fetching: works this paper cited (cites) or
  // works that cited it (cited_by).
  enum class RefKind { Cites, CitedBy };

  // Outcome of one search tier.
  enum class TierResult {
    Matched,     // confident match — work id found
    NoMatch,     // try the next tier
    StopNoMatch, // exact title found but rejected: further tiers are useless
    Transient    // unreadable reply — keep the paper pending
  };

  // One paper to process.  Built on the main thread by the scan.
  struct Job {
    QString paperHash;
    QString dbPath; // DB the job was scanned from (writes are dropped if the
                    // active DB differs)
    QString apiKey; // snapshot of the config (read on the main thread)
    PaperMeta meta;
    QString workId; // already known -> skip the search
    bool needCites = true;
    bool needCitedBy = true;
  };

  // --- Queue (client thread) ---
  void enqueue(const QList<Job> &jobs);
  void startNext();
  // End the current job (success or not) and start the next one.
  void finishCurrent();

  // --- Network (client thread) ---
  // GET @p url; retries 429 / 5xx / timeouts up to kMaxRetries with backoff
  // (honouring Retry-After).  @p handler gets ok=false on a final failure.
  using ReplyHandler = std::function<void(bool ok, const QByteArray &body)>;
  void sendRequest(const QUrl &url, const QString &label, int attempt,
                   ReplyHandler handler);

  // --- Search tiers ---
  void runSearchTier(const QUrlQuery &query, const QString &label,
                     std::function<void()> next);
  void searchByTitle();  // tier 1 -> semanticSearch
  void semanticSearch(); // tier 2 -> searchByAuthor
  void searchByAuthor(); // tier 3 (last)
  TierResult scoreResults(const QByteArray &body, const QString &label,
                          QString *workId) const;
  void onWorkResolved(const QString &workId);

  // --- Citation lists ---
  // Start every missing list of the current job in parallel; finishes the
  // job right away if none is missing.
  void startLists();
  // One list is done (saved or failed); finishes the job after the last one.
  void listDone(bool saved);
  void fetchWorkList(RefKind kind, int page);
  void handleListPage(RefKind kind, int page, const QByteArray &body);
  QUrl buildListUrl(const QString &filterField, const QString &workId,
                    int page) const;
  void appendPage(QUrlQuery &query, int page) const;
  void appendAuthQuery(QUrlQuery &query) const;

  // --- DB (always executed on the main thread) ---
  QList<Job> dbLoadPendingJobs(const QString &apiKey) const;
  void dbSaveWorkId(const Job &job, const QString &workId);
  void dbSaveList(const Job &job, RefKind kind, const QString &itemsJson,
                  int total);
  // Emit referencesSaved from the main thread, after the queued list saves.
  void dbNotifySaved(const Job &job);
  void dbMarkNotFound(const Job &job);
  // Post @p fn to the main thread; it runs only if @p job's DB is still the
  // active, open one.
  void onMainThreadForJob(const Job &job, std::function<void()> fn);

  ConfigManager *m_config = nullptr;     // main thread only
  DatabaseManager *m_database = nullptr; // main thread only (used as context)
  QNetworkAccessManager *m_nam = nullptr;
  QSet<QNetworkReply *> m_pendingReplies; // in flight (up to 2 for lists)
  QThread *m_thread = nullptr;

  // Client-thread state.
  QQueue<Job> m_queue;
  QSet<QString> m_known; // hashes queued or in progress (dedup)
  std::optional<Job> m_current;
  // Per-list buffers, indexed by listIndex(RefKind): the two lists are paged
  // concurrently, so they must not share one.
  QJsonArray m_listBuffer[2];
  int m_listsRunning = 0;      // lists of the current job still in flight
  bool m_anyListSaved = false; // at least one list of the job was saved
  quint64 m_generation = 0; // bumped on DB switch; stale callbacks bail out
};
