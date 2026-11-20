#include "openalex.h"
#include "configmanager.h"
#include "database.h"
#include "metadataresolver.h"
#include "notifier.h"
#include "sqlitedb.h"

#include <QDateTime>
#include <Sqlite3/sqlite3.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>
#include <utility>

// Marker stored in cites/cited_by when OpenAlex has no data for a paper.
// Non-empty so the startup scan doesn't retry it, but distinct from a JSON
// array so the QML pane can show a friendly message.
static const char *kNoReferencesMsg = "No references found on OpenAlex.";

// OpenAlex caps per-page at 100 (200 is deprecated).
static const int kPerPage = 100;

// Hard cap on how many works we store per list.  Seminal papers can be cited
// by thousands of others; we only keep the first kMaxWorks (the most-cited,
// per the sort below) to keep the database lean and the UI responsive.
static const int kMaxWorks = 100;

// Fields requested for each cited/citing work.  primary_location is selected
// whole (nested paths are not valid in select); the journal is read from
// primary_location.source.display_name.  authorships is requested so the
// stored list carries each work's author names; id is the OpenAlex work id.
static const char *kSelectFields =
    "id,display_name,publication_year,doi,primary_location,authorships";

// Retry policy for transient HTTP failures (429 rate limit, 5xx, timeouts).
static const int kMaxRetries = 3;
static const int kTransferTimeoutMs = 30000;

// A paper OpenAlex could not match is not searched again for this long
// (Citations.lookup_status = 'not_found', lookup_at = when).  A paper not
// found by its title that has a DOI in Zotero since is searched again at
// once; not found by its DOI either, it is 'not_found_doi' and waits too.
static const int kNotFoundRetryDays = 30;

/* ── Construction / destruction ─────────────────────────────── */

OpenAlexClient::OpenAlexClient(ConfigManager *config, DatabaseManager *database,
                               QObject *parent)
    : QObject(parent), m_config(config), m_database(database) {
  // m_thread is parented to this; destroying the client (parent) will delete
  // it.  No self-deleteLater here: a deleteLater queued on `this` during
  // destruction races with the stack object in main() being torn down and
  // causes a double free on close.
  m_thread = new QThread(this);
  moveToThread(m_thread);
  m_thread->start();
}

OpenAlexClient::~OpenAlexClient() {
  // Stop the worker thread while this object is still alive.  m_thread is a
  // child of this, so QObject's parent handling deletes it right after this
  // destructor returns — no manual delete needed here.
  if (m_thread && m_thread->isRunning()) {
    m_thread->quit();
    m_thread->wait();
  }
}

/* ── Query helpers ──────────────────────────────────────────── */

void OpenAlexClient::appendAuthQuery(QUrlQuery &query) const {
  const QString key = m_current ? m_current->apiKey : QString();
  if (key.isEmpty())
    return;
  // API keys are [A-Za-z0-9] — use api_key param.
  // Email-style keys use mailto for the polite pool.
  if (key.contains('@'))
    query.addQueryItem("mailto", key);
  else
    query.addQueryItem("api_key", key);
}

void OpenAlexClient::appendPage(QUrlQuery &query, int page) const {
  query.addQueryItem("per-page", QString::number(kPerPage));
  if (page > 1)
    query.addQueryItem("page", QString::number(page));
}

/* ── DB access — main thread only ───────────────────────────── */

void OpenAlexClient::onMainThreadForJob(const Job &job,
                                        std::function<void()> fn) {
  // m_database lives in the main thread, so using it as the context object
  // runs fn there — where the SQLite connection is owned and where
  // switchDatabase() runs, so the two can never interleave.
  QMetaObject::invokeMethod(
      m_database,
      [this, dbPath = job.dbPath, fn = std::move(fn)]() {
        if (!m_database->isOpen() || m_database->databasePath() != dbPath) {
          qInfo() << "[OpenAlex] Project changed — dropping write for" << dbPath;
          return;
        }
        fn();
      },
      Qt::QueuedConnection);
}

QList<OpenAlexClient::Job>
OpenAlexClient::dbLoadPendingJobs(const QString &apiKey) const {
  QList<Job> jobs;
  if (!m_database->isOpen())
    return jobs;

  // Rows missing at least one list, skipping papers OpenAlex recently could
  // not match.  Metadata comes from Summaries (LLM-extracted); the DOI from
  // Zotero (a PDF still in Zotero first).
  auto &db = m_database->db();
  // The Zotero row of each paper: one with a DOI, still in Zotero, first.
  SqliteStatement stmt = db.prepare(
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
          .arg(kNotFoundRetryDays));
  if (!stmt.isValid()) {
    // Zotero's tables unusable: never let them stop the citations.  The same
    // scan without Zotero (empty DOI and metadata columns).
    qWarning() << "[OpenAlex] Scan with Zotero's DOIs failed:"
               << QString::fromUtf8(sqlite3_errmsg(db.handle())) << "— scanning without them";
    stmt = db.prepare(
        QString("SELECT c.paper_hash, c.work_id, c.cites = '', c.cited_by = '', "
                "COALESCE(NULLIF(s.title, ''), c.clean_title), s.authors, "
                "s.year, s.journal, '', 1, '', '[]', '', '' "
                "FROM Citations c "
                "LEFT JOIN Summaries s ON s.paper_hash = c.paper_hash "
                "WHERE (c.cites = '' OR c.cited_by = '') "
                "AND NOT (c.lookup_status IN ('not_found', 'not_found_doi') "
                "AND c.lookup_at > datetime('now', '-%1 days'))")
            .arg(kNotFoundRetryDays));
  }
  if (!stmt.isValid()) {
    qWarning() << "[OpenAlex] Scan: prepare failed (old schema? delete the "
                  "project DB to recreate the Citations table)";
    return jobs;
  }

  const QString dbPath = m_database->databasePath();
  while (true) {
    auto step = stmt.step();
    if (!step || !*step)
      break;
    Job job;
    job.paperHash = stmt.columnText(0);
    job.workId = stmt.columnText(1).trimmed();
    job.needCites = stmt.columnInt(2) != 0;
    job.needCitedBy = stmt.columnInt(3) != 0;
    job.meta.cleanTitle = stmt.columnText(4).trimmed();
    for (const QString &a : stmt.columnText(5).split(';', Qt::SkipEmptyParts))
      if (!a.trimmed().isEmpty())
        job.meta.authors << a.trimmed();
    job.meta.year = stmt.columnText(6).trimmed().toInt();
    job.meta.journal = stmt.columnText(7).trimmed();
    job.doi = normalizeDoi(stmt.columnText(8));
    // Not summarized: the title is a file name and there are no authors —
    // Zotero's metadata is better for the search (when the DOI fails).
    const bool summarized = stmt.columnInt(9) != 0;
    const QString zoteroTitle = stmt.columnText(10).trimmed();
    if (!summarized && !zoteroTitle.isEmpty()) {
      job.meta.cleanTitle = zoteroTitle;
      job.meta.authors.clear();
      for (const QJsonValue &a : QJsonDocument::fromJson(stmt.columnText(11).toUtf8()).array())
        job.meta.authors << MetadataResolver::displayName(a.toString());
      job.meta.year = stmt.columnText(12).trimmed().toInt();
      job.meta.journal = stmt.columnText(13).trimmed();
    }
    job.dbPath = dbPath;
    job.apiKey = apiKey;
    jobs << job;
  }
  return jobs;
}

int OpenAlexClient::dbRegisterDoiPapers() const {
  if (!m_database->isOpen())
    return 0;
  // Papers of the project (a Summaries row is made at import) with a DOI in
  // Zotero, not in Citations yet: Zotero's title until a summary replaces it.
  auto r = m_database->db().exec(
      "INSERT INTO Citations (paper_hash, clean_title) "
      "SELECT z.paper_hash, MAX(z.title) FROM ZoteroItems z "
      "WHERE z.doi <> '' "
      "AND EXISTS (SELECT 1 FROM Summaries s WHERE s.paper_hash = z.paper_hash) "
      "AND NOT EXISTS (SELECT 1 FROM Citations c WHERE c.paper_hash = z.paper_hash) "
      "GROUP BY z.paper_hash");
  if (!r) {
    qWarning() << "[OpenAlex] Registering the papers with a DOI failed:" << r.error();
    return 0;
  }
  return int(sqlite3_changes(m_database->db().handle()));
}

void OpenAlexClient::dbSaveWorkId(const Job &job, const QString &workId) {
  onMainThreadForJob(job, [this, hash = job.paperHash, workId]() {
    SqliteStatement stmt = m_database->db().prepare(
        "UPDATE Citations SET work_id = ?, lookup_status = 'matched', "
        "lookup_at = CURRENT_TIMESTAMP WHERE paper_hash = ?");
    if (!stmt.isValid())
      return;
    (void)stmt.bind(1, workId);
    (void)stmt.bind(2, hash);
    (void)stmt.exec();
  });
}

void OpenAlexClient::dbMarkNotFound(const Job &job) {
  // 'not_found_doi': the DOI was tried too, nothing new to try before the
  // retry delay.
  const QString status = job.doi.isEmpty() ? QStringLiteral("not_found")
                                           : QStringLiteral("not_found_doi");
  onMainThreadForJob(job, [this, hash = job.paperHash, status]() {
    SqliteStatement stmt = m_database->db().prepare(
        "UPDATE Citations SET lookup_status = ?, "
        "lookup_at = CURRENT_TIMESTAMP WHERE paper_hash = ?");
    if (!stmt.isValid())
      return;
    (void)stmt.bind(1, status);
    (void)stmt.bind(2, hash);
    (void)stmt.exec();
  });
}

void OpenAlexClient::dbSaveList(const Job &job, RefKind kind,
                                const QString &itemsJson, int total) {
  onMainThreadForJob(job, [this, hash = job.paperHash, kind, itemsJson,
                           total]() {
    const char *col = (kind == RefKind::Cites) ? "cites" : "cited_by";
    const char *countCol =
        (kind == RefKind::Cites) ? "cites_count" : "cited_by_count";
    SqliteStatement stmt = m_database->db().prepare(
        QString("UPDATE Citations SET %1 = ?, %2 = ? WHERE paper_hash = ?")
            .arg(QLatin1String(col), QLatin1String(countCol)));
    if (!stmt.isValid())
      return;
    (void)stmt.bind(1, itemsJson);
    (void)stmt.bind(2, total);
    (void)stmt.bind(3, hash);
    if (stmt.exec())
      qInfo() << "[OpenAlex] Saved" << itemsJson.size() << "bytes of" << col
              << "(total" << total << ") for" << hash.left(12);
  });
}

void OpenAlexClient::dbNotifySaved(const Job &job) {
  // Queued on the main thread AFTER the list saves of this job, so the models
  // reload once with both lists in place.  Emitted from the main thread:
  // DatabaseManager::reloadModels runs directly, on the thread that owns the
  // models.
  onMainThreadForJob(
      job, [this, hash = job.paperHash]() { emit referencesSaved(hash); });
}

/* ── Scan + queue ───────────────────────────────────────────── */

void OpenAlexClient::checkEmptyReferences() {
  // Read config + DB on the main thread, then hand the jobs to the client
  // thread.
  QMetaObject::invokeMethod(
      m_database,
      [this]() {
        const QString key = m_config->openAlexApiKey().trimmed();
        if (const int added = dbRegisterDoiPapers(); added > 0)
          qInfo() << "[OpenAlex] Scan:" << added
                  << "paper(s) with a DOI in Zotero registered without waiting for a summary";
        const QList<Job> jobs = dbLoadPendingJobs(key);
        if (jobs.isEmpty()) {
          qInfo() << "[OpenAlex] Scan: no empty citations to fetch";
          return;
        }
        qInfo() << "[OpenAlex] Scan:" << jobs.size()
                << "paper(s) with missing citation data";
        QMetaObject::invokeMethod(
            this, [this, jobs]() { enqueue(jobs); }, Qt::QueuedConnection);
      },
      Qt::QueuedConnection);
}

void OpenAlexClient::onDatabaseSwitched() {
  // Invalidate everything belonging to the previous DB.  Bump the generation
  // BEFORE aborting: abort() emits finished() synchronously and the handler
  // must see the reply as stale.
  ++m_generation;
  m_queue.clear();
  m_known.clear();
  m_current.reset();
  m_listBuffer[0] = m_listBuffer[1] = QJsonArray();
  m_listsRunning = 0;
  m_anyListSaved = false;
  // abort() emits finished() synchronously, which removes the reply from the
  // set — iterate over a copy.
  const QSet<QNetworkReply *> pending = std::exchange(m_pendingReplies, {});
  for (QNetworkReply *r : pending)
    r->abort();
  qInfo() << "[OpenAlex] Project switched — queue flushed, rescanning";
  checkEmptyReferences();
}

void OpenAlexClient::enqueue(const QList<Job> &jobs) {
  int added = 0;
  for (const Job &job : jobs) {
    if (m_known.contains(job.paperHash))
      continue; // already queued or in progress
    m_known.insert(job.paperHash);
    m_queue.enqueue(job);
    ++added;
  }
  if (added)
    qInfo() << "[OpenAlex] Queued" << added << "paper(s) |" << m_queue.size()
            << "waiting";
  startNext();
}

void OpenAlexClient::startNext() {
  if (m_current || m_queue.isEmpty())
    return;
  m_current = m_queue.dequeue();
  m_listBuffer[0] = m_listBuffer[1] = QJsonArray();
  m_listsRunning = 0;
  m_anyListSaved = false;
  if (!m_current->workId.isEmpty()) {
    // Work already identified on a previous run — only the missing lists.
    startLists();
    return;
  }
  if (!m_current->doi.isEmpty())
    searchByDoi();
  else
    searchByTitle();
}

void OpenAlexClient::finishCurrent() {
  if (m_current)
    m_known.remove(m_current->paperHash);
  m_current.reset();
  m_listBuffer[0] = m_listBuffer[1] = QJsonArray();
  m_listsRunning = 0;
  m_anyListSaved = false;
  // Queued so a long queue never recurses on the stack.
  QMetaObject::invokeMethod(this, &OpenAlexClient::startNext,
                            Qt::QueuedConnection);
}

/* ── Network ────────────────────────────────────────────────── */

void OpenAlexClient::sendRequest(const QUrl &url, const QString &label,
                                 int attempt, ReplyHandler handler) {
  if (!m_nam)
    m_nam = new QNetworkAccessManager(this);

  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::UserAgentHeader, "Hodeia/0.1");
  req.setTransferTimeout(kTransferTimeoutMs);

  const quint64 gen = m_generation;
  QNetworkReply *reply = m_nam->get(req);
  m_pendingReplies.insert(reply);
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, gen, url, label, attempt,
           handler = std::move(handler)]() {
            m_pendingReplies.remove(reply);
            reply->deleteLater();
            if (gen != m_generation)
              return; // project switched meanwhile — drop silently

            if (reply->error() == QNetworkReply::NoError) {
              handler(true, reply->readAll());
              return;
            }

            const int http =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                    .toInt();
            const bool retryable =
                http == 429 || http >= 500 ||
                reply->error() == QNetworkReply::OperationCanceledError ||
                reply->error() == QNetworkReply::TimeoutError;
            if (retryable && attempt < kMaxRetries) {
              int delayMs = 2000 << attempt; // 2 s, 4 s, 8 s
              bool ok = false;
              const int retryAfter =
                  reply->rawHeader("Retry-After").trimmed().toInt(&ok);
              if (ok && retryAfter > 0)
                delayMs = qMin(retryAfter, 60) * 1000;
              qWarning() << "[OpenAlex] ──" << label << "── HTTP" << http
                         << reply->errorString() << "| retry" << attempt + 1
                         << "/" << kMaxRetries << "in" << delayMs << "ms";
              QTimer::singleShot(delayMs, this,
                                 [this, gen, url, label, attempt, handler]() {
                                   if (gen != m_generation)
                                     return;
                                   sendRequest(url, label, attempt + 1,
                                               handler);
                                 });
              return;
            }

            qWarning() << "[OpenAlex] ──" << label << "── ERROR:"
                       << reply->errorString() << "| HTTP:" << http;
            // Other failures (offline, timeouts) are retried on the next
            // scan and don't need the user; these two do.
            if (http == 401 || http == 403) {
              Notifier::post(
                  "openalex-auth", tr("OpenAlex Access Refused"),
                  tr("OpenAlex refused the request (HTTP %1), so citations "
                     "cannot be fetched. The API key is probably invalid or "
                     "expired: check it in the OpenAlex API Key menu.")
                      .arg(http));
            } else if (http == 429) {
              Notifier::post(
                  "openalex-limit", tr("OpenAlex Limit Reached"),
                  tr("OpenAlex limits the number of requests and the limit "
                     "is reached, so some citations could not be fetched. "
                     "They will be fetched again at a later start. An "
                     "OpenAlex API key raises the limit."),
                  false);
            }
            handler(false, QByteArray());
          });
}

/* ── Step 1: find the work ID (title → semantic → author) ───── */

// Fields requested for each search-tier candidate (authorships includes
// raw_author_name, the name as printed in the paper).
static const char *kSearchSelect =
    "id,display_name,authorships,publication_year,primary_location";

void OpenAlexClient::runSearchTier(const QUrlQuery &query,
                                   const QString &label,
                                   std::function<void()> next) {
  QUrl url("https://api.openalex.org/works");
  url.setQuery(query);
  const QString hash = m_current->paperHash;
  qInfo() << "[OpenAlex] ──" << label << "──"
          << "paper:" << hash.left(12)
          << "| URL:" << url.toString(QUrl::FullyEncoded);

  sendRequest(url, label, 0,
              [this, label, hash, next = std::move(next)](
                  bool ok, const QByteArray &body) {
                if (!ok) {
                  // Network / HTTP failure after retries: NOT "not found" —
                  // the paper stays pending and the next scan retries it.
                  qWarning() << "[OpenAlex] ──" << label << "──"
                             << "paper:" << hash.left(12)
                             << "| giving up for now (will retry)";
                  finishCurrent();
                  return;
                }
                QString workId;
                switch (scoreResults(body, label, &workId)) {
                case TierResult::Matched:
                  onWorkResolved(workId);
                  return;
                case TierResult::Transient:
                  finishCurrent();
                  return;
                case TierResult::StopNoMatch:
                  qWarning() << "[OpenAlex] ──" << label << "──"
                             << "paper:" << hash.left(12)
                             << "| exact title found but authors disagree — "
                                "not searching further";
                  dbMarkNotFound(*m_current);
                  finishCurrent();
                  return;
                case TierResult::NoMatch:
                  if (next) {
                    next();
                  } else {
                    qWarning() << "[OpenAlex] ── NO MATCH ──"
                               << "paper:" << hash.left(12)
                               << "| no confident match across title, "
                                  "semantic, author";
                    dbMarkNotFound(*m_current);
                    finishCurrent();
                  }
                  return;
                }
              });
}

QString OpenAlexClient::normalizeDoi(const QString &doi) {
  QString d = doi.trimmed().toLower();
  for (const char *prefix : {"https://doi.org/", "http://doi.org/", "https://dx.doi.org/",
                             "http://dx.doi.org/", "doi.org/", "doi:"})
    if (d.startsWith(QLatin1String(prefix))) {
      d = d.mid(qstrlen(prefix)).trimmed();
      break;
    }
  // "10.<registrant>/<suffix>"
  static const QRegularExpression doiPattern(QStringLiteral("^10\\.\\d{4,9}/\\S+$"));
  return doiPattern.match(d).hasMatch() ? d : QString();
}

QString OpenAlexClient::workIdOf(const QByteArray &body) {
  const QJsonObject work = QJsonDocument::fromJson(body).object();
  // id is a URL like "https://openalex.org/W1234567890"
  const QString id = work.value("id").toString().section('/', -1);
  return id.startsWith('W') ? id : QString();
}

void OpenAlexClient::searchByDoi() {
  // The DOI identifies the work: no scoring needed.  Zotero's DOI is taken
  // over the local metadata (often a file name for an unsummarized paper).
  QUrl url(QStringLiteral("https://api.openalex.org/works/doi:") + m_current->doi);
  QUrlQuery query;
  query.addQueryItem("select", kSearchSelect);
  appendAuthQuery(query);
  url.setQuery(query);
  const QString hash = m_current->paperHash;
  const QString doi = m_current->doi;
  qInfo() << "[OpenAlex] ── DOI ──"
          << "paper:" << hash.left(12) << "| DOI:" << doi;
  sendRequest(url, "DOI", 0, [this, hash, doi](bool ok, const QByteArray &body) {
    const QString workId = ok ? workIdOf(body) : QString();
    if (!workId.isEmpty()) {
      qInfo() << "[OpenAlex] ── DOI ──"
              << "paper:" << hash.left(12) << "| workId:" << workId << "|"
              << QJsonDocument::fromJson(body).object().value("display_name").toString().left(80);
      onWorkResolved(workId);
      return;
    }
    // Unknown to OpenAlex (404), or unreadable: the usual search.
    qInfo() << "[OpenAlex] ── DOI ──"
            << "paper:" << hash.left(12) << "| DOI" << doi
            << "not found — falling back to the title search";
    searchByTitle();
  });
}

void OpenAlexClient::searchByTitle() {
  // Tier 1: exact-title search (what the OpenAlex website uses) — the most
  // precise signal, so it runs first.
  const QString title = m_current->meta.cleanTitle;
  if (title.isEmpty()) {
    searchByAuthor(); // nothing to search by title
    return;
  }
  QUrlQuery query;
  query.addQueryItem("search.title", title);
  query.addQueryItem("sort", "relevance_score:desc");
  query.addQueryItem("per-page", "20");
  query.addQueryItem("select", kSearchSelect);
  appendAuthQuery(query);
  runSearchTier(query, "TITLE", [this]() { semanticSearch(); });
}

void OpenAlexClient::semanticSearch() {
  // Tier 2: embedding-based semantic search, for when the right work is
  // missing from the title-indexed results.  Keep punctuation — the query is
  // embedded, so it tolerates it.
  QUrlQuery query;
  query.addQueryItem("search.semantic", m_current->meta.cleanTitle);
  query.addQueryItem("sort", "relevance_score:desc");
  query.addQueryItem("per-page", "20");
  query.addQueryItem("select", kSearchSelect);
  appendAuthQuery(query);
  runSearchTier(query, "SEMANTIC", [this]() { searchByAuthor(); });
}

void OpenAlexClient::searchByAuthor() {
  // Tier 3 (last resort): search by the first author's name.
  // raw_author_name.search is the only author-name filter on /works
  // (author.display_name.search is not a valid field).
  const QStringList &authors = m_current->meta.authors;
  if (authors.isEmpty()) {
    qWarning() << "[OpenAlex] ── NO MATCH ──"
               << "paper:" << m_current->paperHash.left(12)
               << "| no title match and no authors to search by";
    dbMarkNotFound(*m_current);
    finishCurrent();
    return;
  }
  QUrlQuery query;
  query.addQueryItem("filter", QString("raw_author_name.search:%1")
                                   .arg(authors.first().trimmed()));
  query.addQueryItem("sort", "publication_year:desc");
  query.addQueryItem("per-page", "50");
  query.addQueryItem("select", kSearchSelect);
  appendAuthQuery(query);
  runSearchTier(query, "AUTHOR", {}); // last tier — no fallback
}

/* ── Candidate disambiguation ────────────────────────────────── */

// Case-insensitive containment of a substring (needle) in haystack.
static bool containsCI(const QString &haystack, const QString &needle) {
  const QString n = needle.trimmed();
  if (n.isEmpty())
    return false;
  return haystack.toLower().contains(n.toLower());
}

// Levenshtein edit-distance thresholds (percent of the shorter string's
// length) for the fuzzy fields in bestCandidate.  Authors are matched
// most loosely (middle initials, transliterations, diacritics), title and
// journal more strictly.
static constexpr int kAuthorFuzzyPct = 40;
static constexpr int kTitleFuzzyPct = 30;
static constexpr int kJournalFuzzyPct = 30;

// Normalise a string for fuzzy comparison: lowercase, keep letters and
// digits only (drops punctuation, whitespace, symbols).  Diacritics and
// special Unicode chars (ß, ü, é, …) are kept as-is — the Levenshtein
// threshold in fuzzyMatch absorbs the small edit distance they add
// (e.g. "cornelissen" vs "cornelißen" = 2 edits).
static QString normalizeName(const QString &s) {
  QString out;
  out.reserve(s.size());
  // NFKD splits accented letters into base letter + combining mark; the mark
  // is not a letter, so it is dropped (é → e, ü → u).  ß and the like stay.
  for (const QChar &c : s.normalized(QString::NormalizationForm_KD).toLower())
    if (c.isLetterOrNumber())
      out.append(c);
  return out;
}

// Levenshtein edit distance (iterative, O(n·m)).
static int levenshtein(const QString &a, const QString &b) {
  const int n = a.size(), m = b.size();
  if (n == 0)
    return m;
  if (m == 0)
    return n;
  QVector<int> prev(m + 1), cur(m + 1);
  for (int j = 0; j <= m; ++j)
    prev[j] = j;
  for (int i = 1; i <= n; ++i) {
    cur[0] = i;
    for (int j = 1; j <= m; ++j) {
      const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      cur[j] = qMin(qMin(prev[j] + 1, cur[j - 1] + 1), prev[j - 1] + cost);
    }
    std::swap(prev, cur);
  }
  return prev[m];
}

// Fuzzy match: normalise both strings, then accept if one contains the
// other or the Levenshtein distance is within @p pct percent of the
// shorter string's length.  pct=30 means "allow up to 30% edits."
static bool fuzzyMatch(const QString &a, const QString &b, int pct) {
  const QString na = normalizeName(a);
  const QString nb = normalizeName(b);
  if (na.isEmpty() || nb.isEmpty())
    return false;
  if (na == nb || na.contains(nb) || nb.contains(na))
    return true;
  const int dist = levenshtein(na, nb);
  const int len = qMin(na.size(), nb.size());
  return dist * 100 <= len * pct;
}

// Split a person name into normalised tokens and identify the surname.
// Handles "First M. Last", "F. M. Last", "F.M. Last" and "Last, First M.".
// Returns false if no usable surname is found.
struct NameParts {
  QString surname;      // normalised (letters/digits only)
  QStringList given;    // normalised given names / initials, in order
};

static bool splitName(const QString &raw, NameParts &out) {
  QString s = raw.trimmed();
  QString surnamePart, givenPart;
  const int comma = s.indexOf(',');
  if (comma > 0) {
    // "Last, First M."
    surnamePart = s.left(comma);
    givenPart = s.mid(comma + 1);
  } else {
    // "First M. Last" — the last whitespace-separated word is the surname.
    QString spaced = s;
    spaced.replace('.', ". ");
    const QStringList words = spaced.simplified().split(' ', Qt::SkipEmptyParts);
    if (words.isEmpty())
      return false;
    surnamePart = words.last();
    givenPart = words.mid(0, words.size() - 1).join(' ');
  }
  out.surname = normalizeName(surnamePart);
  if (out.surname.isEmpty())
    return false;
  // Given names: dots and hyphens separate initials ("C.D.", "Jean-Paul").
  givenPart.replace('.', ' ');
  givenPart.replace('-', ' ');
  out.given.clear();
  for (const QString &w : givenPart.split(' ', Qt::SkipEmptyParts)) {
    const QString n = normalizeName(w);
    if (!n.isEmpty())
      out.given << n;
  }
  return true;
}

// Two given-name tokens are compatible if either is an initial and they share
// the first letter ("c" ~ "chris"), or both are full names that fuzzy-match.
static bool givenCompatible(const QString &a, const QString &b) {
  if (a.size() == 1 || b.size() == 1)
    return a.at(0) == b.at(0);
  return fuzzyMatch(a, b, kAuthorFuzzyPct);
}

// Surname comparison (inputs already normalised).  Stricter than fuzzyMatch:
// containment is only accepted when the shorter name has >= 5 letters, so
// compound names match ("garcia" ~ "garcialopez") but short ones do not leak
// ("li" !~ "williams").  Otherwise <= 30% Levenshtein edits.
static bool surnameMatches(const QString &a, const QString &b) {
  if (a == b)
    return true;
  const int len = qMin(a.size(), b.size());
  if (len >= 5 && (a.contains(b) || b.contains(a)))
    return true;
  return levenshtein(a, b) * 100 <= len * kTitleFuzzyPct;
}

// Initials-aware author comparison.  The surnames must match (fuzzy, for
// transliterations/diacritics); then every given token on the side with
// FEWER given names must be compatible with a distinct token on the other
// side (so "C. D. Orme" ~ "Chris D. Orme", "T. A. Barmby" ~ "Tim Barmby",
// but "C. Orme" !~ "David Orme").  Falls back to the whole-string fuzzy match
// only for names that do not split at all.
static bool authorMatches(const QString &local, const QString &cand) {
  NameParts a, b;
  if (!splitName(local, a) || !splitName(cand, b))
    return fuzzyMatch(local, cand, kAuthorFuzzyPct);

  if (!surnameMatches(a.surname, b.surname))
    return false;

  const QStringList &fewer = a.given.size() <= b.given.size() ? a.given : b.given;
  const QStringList &more = a.given.size() <= b.given.size() ? b.given : a.given;
  QList<bool> used(more.size(), false);
  for (const QString &g : fewer) {
    bool ok = false;
    for (int j = 0; j < more.size(); ++j) {
      if (!used[j] && givenCompatible(g, more[j])) {
        used[j] = true;
        ok = true;
        break;
      }
    }
    if (!ok)
      return false;
  }
  return true;
}

// OpenAlex stores duplicate records for the same paper (e.g. the
// journal-version of record plus a repository/working-copy version).
// The journal version is the canonical one and the one that carries
// the real citation graph, so we prefer it when disambiguating.
static bool preferWork(const QJsonObject &a, const QJsonObject &b) {
  const auto locName = [](const QJsonObject &w) {
    return w.value("primary_location")
        .toObject()
        .value("source")
        .toObject()
        .value("display_name")
        .toString()
        .trimmed();
  };
  const QString la = locName(a);
  const QString lb = locName(b);
  const bool ea = la.isEmpty() || la == "N/A";
  const bool eb = lb.isEmpty() || lb == "N/A";
  if (ea != eb)
    return !ea; // a has a real venue
  if (ea && eb)
    return false;
  return la < lb; // deterministic tiebreak
}

int OpenAlexClient::bestCandidate(const QJsonArray &results,
                                  const PaperMeta &meta,
                                  bool *exactTitleRejected) {
  if (exactTitleRejected)
    *exactTitleRejected = false;
  int bestIdx = -1;
  int bestScore = 0;
  const QString normLocalTitle = normalizeName(meta.cleanTitle);

  for (int i = 0; i < results.size(); ++i) {
    const QJsonObject w = results.at(i).toObject();
    int score = 0;

    // 1. Title (strongest signal) — fuzzy, kTitleFuzzyPct threshold.
    const QString candTitle = w.value("display_name").toString();
    const bool titleExact =
        !normLocalTitle.isEmpty() && normLocalTitle == normalizeName(candTitle);
    int titleScore = 0;
    if (!meta.cleanTitle.isEmpty() &&
        fuzzyMatch(meta.cleanTitle, candTitle, kTitleFuzzyPct))
      titleScore = 3;
    else if (containsCI(candTitle, meta.cleanTitle) ||
             containsCI(meta.cleanTitle, candTitle))
      titleScore = 2;
    score += titleScore;

    // 2. Authors — compare each local author (LLM-extracted, often with
    //    initials) against both the canonical display_name and the name as
    //    printed in the paper (raw_author_name) of every candidate author.
    int matchedAuthors = 0;
    bool firstAuthorMatch = false;
    if (!meta.authors.isEmpty()) {
      QStringList candAuthors;
      for (const QJsonValue &av : w.value("authorships").toArray()) {
        const QJsonObject as = av.toObject();
        const QString display =
            as.value("author").toObject().value("display_name").toString();
        const QString rawName = as.value("raw_author_name").toString();
        if (!display.isEmpty())
          candAuthors << display;
        if (!rawName.isEmpty() && rawName != display)
          candAuthors << rawName;
      }
      for (int k = 0; k < meta.authors.size(); ++k) {
        const QString a = meta.authors.at(k).trimmed();
        for (const QString &ca : candAuthors) {
          if (authorMatches(a, ca)) {
            ++matchedAuthors;
            if (k == 0)
              firstAuthorMatch = true;
            break;
          }
        }
      }
    }
    const int nAuthors = meta.authors.size();
    // Strict majority of the local authors found among the candidate's.
    const bool authorsMajority = nAuthors > 0 && matchedAuthors * 2 > nAuthors;
    if (authorsMajority)
      score += 2;

    // 3. Year — allow ±1 to tolerate online-first vs. issue-date mismatches
    //    (e.g. local says 2004, OpenAlex says 2003 for the same paper).
    bool yearOk = false;
    if (meta.year > 0) {
      const int candYear = w.value("publication_year").toInt();
      yearOk = qAbs(candYear - meta.year) <= 1;
      if (yearOk)
        score += 1;
    }

    // 4. Journal — fuzzy, kJournalFuzzyPct threshold.
    bool journalOk = false;
    if (!meta.journal.isEmpty()) {
      const QString candJournal = w.value("primary_location")
                                      .toObject()
                                      .value("source")
                                      .toObject()
                                      .value("display_name")
                                      .toString();
      journalOk = fuzzyMatch(meta.journal, candJournal, kJournalFuzzyPct);
      if (journalOk)
        score += 1;
    }

    // Author gate: OpenAlex has many works sharing a (near-)identical title by
    // different authors, so a candidate must agree on authors.  The local
    // list is LLM-extracted (may be incomplete or misspelt), so we do not
    // demand ALL of them:
    //   * normally a strict majority of local authors must match;
    //   * if the title is identical (normalised) and the year agrees, the
    //     first author alone is enough.
    if (nAuthors > 0) {
      const bool strongTitle = titleExact && yearOk && firstAuthorMatch;
      if (!authorsMajority && !strongTitle) {
        if (titleExact && exactTitleRejected)
          *exactTitleRejected = true;
        continue;
      }
    }

    // Title gate: a candidate whose title does not match at all is only
    // accepted on the full authors + year + journal combination.  Authors +
    // year alone used to be enough (score 3), which matched a single-author
    // paper to ANY other paper by the same author from the same year.
    if (titleScore == 0 && !(authorsMajority && yearOk && journalOk))
      continue;

    // Prefer the first candidate strictly better than any seen so far.
    if (score > bestScore) {
      bestScore = score;
      bestIdx = i;
    } else if (score == bestScore && bestIdx >= 0 &&
               preferWork(w, results.at(bestIdx).toObject())) {
      // Same score — break ties toward the journal version of record
      // (see preferWork).
      bestIdx = i;
    }
  }

  // Require a confident match: at least a title hit (>=2) or a strong
  // authors+year+journal combination.  A bare year or journal alone is too
  // weak on its own.
  if (bestScore < 2)
    return -1;
  return bestIdx;
}

/* ── Disambiguation: pick the work ID, then fetch the lists ─── */

OpenAlexClient::TierResult
OpenAlexClient::scoreResults(const QByteArray &body, const QString &label,
                             QString *workId) const {
  const QString hash = m_current->paperHash.left(12);
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
  if (err.error != QJsonParseError::NoError) {
    qWarning() << "[OpenAlex] ──" << label << "──"
               << "paper:" << hash << "| JSON parse error:" << err.errorString();
    return TierResult::Transient;
  }

  const QJsonArray results = doc.object().value("results").toArray();
  if (results.isEmpty()) {
    qWarning() << "[OpenAlex] ──" << label << "──"
               << "paper:" << hash << "| No works returned";
    return TierResult::NoMatch;
  }

  bool exactTitleRejected = false;
  const int best = bestCandidate(results, m_current->meta, &exactTitleRejected);
  if (best < 0) {
    qWarning() << "[OpenAlex] ──" << label << "──"
               << "paper:" << hash << "| No confident match among"
               << results.size() << "candidates";
    return exactTitleRejected ? TierResult::StopNoMatch : TierResult::NoMatch;
  }

  // id is a URL like "https://openalex.org/W1234567890"
  *workId = results.at(best).toObject().value("id").toString().section('/', -1);
  qInfo() << "[OpenAlex] ──" << label << "──"
          << "paper:" << hash << "| candidates:" << results.size()
          << "| chosen idx:" << best << "| workId:" << *workId;
  return TierResult::Matched;
}

void OpenAlexClient::onWorkResolved(const QString &workId) {
  m_current->workId = workId;
  dbSaveWorkId(*m_current, workId);
  startLists();
}

/* ── Step 2/3: paginated fetch of a citation list ───────────── */

static int listIndex(bool isCites) { return isCites ? 0 : 1; }

void OpenAlexClient::startLists() {
  // Set the counter before starting either request so listDone() always
  // sees the full count.
  m_listsRunning = (m_current->needCites ? 1 : 0) +
                   (m_current->needCitedBy ? 1 : 0);
  m_anyListSaved = false;
  if (m_listsRunning == 0) {
    finishCurrent();
    return;
  }
  if (m_current->needCites)
    fetchWorkList(RefKind::Cites, 1);
  if (m_current->needCitedBy)
    fetchWorkList(RefKind::CitedBy, 1);
}

void OpenAlexClient::listDone(bool saved) {
  if (saved)
    m_anyListSaved = true;
  if (--m_listsRunning > 0)
    return; // the other list is still running
  if (m_anyListSaved)
    dbNotifySaved(*m_current);
  // A failed list stays '' with the work id saved: the next scan fetches
  // only that list, without searching again.
  finishCurrent();
}

// Build the URL for one page of a citation list for @p workId.  @p
// filterField is "cites" (works citing W) or "cited_by" (works W cited).
// Sorted most-cited first so the first kMaxWorks kept are the most important.
//
// Column semantics (from the user's standpoint of the queried work W):
//   "cites"    = the works that CITE W (its citers)
//   "cited_by" = the works W CITED (its references)
//
// OpenAlex /works filter semantics (verified empirically):
//   cites:{W}    -> works that cite W          (its citers)
//   cited_by:{W} -> works that W cited         (its references)
//   (referenced_works:{W} is a synonym for cites:{W} — it also returns
//    citers, NOT the references, so it must not be used here.)
QUrl OpenAlexClient::buildListUrl(const QString &filterField,
                                  const QString &workId, int page) const {
  QUrl url("https://api.openalex.org/works");
  QUrlQuery query;
  query.addQueryItem("filter", QString("%1:%2").arg(filterField, workId));
  query.addQueryItem("select", kSelectFields);
  query.addQueryItem("sort", "cited_by_count:desc");
  appendPage(query, page);
  appendAuthQuery(query);
  url.setQuery(query);
  return url;
}

void OpenAlexClient::fetchWorkList(RefKind kind, int page) {
  const char *filterField = (kind == RefKind::Cites) ? "cites" : "cited_by";
  const QString label = (kind == RefKind::Cites) ? "CITES" : "CITED-BY";
  const QUrl url = buildListUrl(filterField, m_current->workId, page);

  if (page == 1)
    m_listBuffer[listIndex(kind == RefKind::Cites)] = QJsonArray();

  qInfo() << "[OpenAlex] ──" << label << "──"
          << "paper:" << m_current->paperHash.left(12) << "| page:" << page
          << "| URL:" << url.toString(QUrl::FullyEncoded);

  sendRequest(url, label, 0,
              [this, kind, page](bool ok, const QByteArray &body) {
                if (!ok) {
                  // The list stays '' and the work id is saved, so the next
                  // scan resumes here without searching again.
                  listDone(false);
                  return;
                }
                handleListPage(kind, page, body);
              });
}

void OpenAlexClient::handleListPage(RefKind kind, int page,
                                    const QByteArray &body) {
  const char *label = (kind == RefKind::Cites) ? "CITES" : "CITED-BY";
  const QString hash = m_current->paperHash;
  QJsonArray &buffer = m_listBuffer[listIndex(kind == RefKind::Cites)];

  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
  if (err.error != QJsonParseError::NoError) {
    qWarning() << "[OpenAlex]" << label
               << "JSON parse error:" << err.errorString();
    listDone(false);
    return;
  }

  const int totalCount =
      doc.object().value("meta").toObject().value("count").toInt();
  const QJsonArray results = doc.object().value("results").toArray();
  qInfo() << "[OpenAlex] ──" << label << "──"
          << "paper:" << hash.left(12) << "| page:" << page
          << "| results:" << results.size() << "| total:" << totalCount;

  // Reduce each result to the fields we store (id, title, authors, year, doi,
  // journal).
  for (const QJsonValue &v : results) {
    const QJsonObject w = v.toObject();
    const QJsonObject loc =
        w.value("primary_location").toObject().value("source").toObject();
    QJsonObject item;
    item["id"] = w.value("id").toString().section('/', -1);
    item["display_name"] = w.value("display_name");
    item["publication_year"] = w.value("publication_year");
    item["doi"] = w.value("doi");
    item["journal"] = loc.value("display_name");

    QStringList authors;
    for (const QJsonValue &av : w.value("authorships").toArray()) {
      const QString name = av.toObject()
                               .value("author")
                               .toObject()
                               .value("display_name")
                               .toString();
      if (!name.isEmpty())
        authors << name;
    }
    item["authors"] = authors.join(QString(QChar(0x1F)));
    buffer.append(item);
  }

  const int buffered = static_cast<int>(buffer.size());
  bool hasMore = buffered < totalCount && !results.isEmpty();
  if (hasMore && buffered >= kMaxWorks) {
    qInfo() << "[OpenAlex] ──" << label << "──"
            << "paper:" << hash.left(12) << "| capping at" << kMaxWorks
            << "of" << totalCount;
    QJsonArray capped;
    for (int i = 0; i < kMaxWorks; ++i)
      capped.append(buffer.at(i));
    buffer = std::move(capped);
    hasMore = false; // we have enough — fall through to save
  }

  if (hasMore) {
    fetchWorkList(kind, page + 1);
    return;
  }

  const QString itemsJson =
      buffer.isEmpty()
          ? QString::fromLatin1(kNoReferencesMsg)
          : QString::fromUtf8(
                QJsonDocument(buffer).toJson(QJsonDocument::Compact));
  buffer = QJsonArray();

  if (kind == RefKind::Cites)
    m_current->needCites = false;
  else
    m_current->needCitedBy = false;
  dbSaveList(*m_current, kind, itemsJson, totalCount);
  listDone(true);
}
