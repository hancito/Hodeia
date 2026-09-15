#include "openalex.h"
#include "configmanager.h"
#include "database.h"
#include "sqlitedb.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>

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
  QString key = m_config->openAlexApiKey().trimmed();
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

/* ── Startup / trigger scan ─────────────────────────────────── */

void OpenAlexClient::checkEmptyReferences() {
  QMetaObject::invokeMethod(
      this,
      [this]() {
        if (!m_database || !m_database->isOpen()) {
          qWarning() << "[OpenAlex] DB not open — skipping scan";
          return;
        }

        auto &db = m_database->db();
        SqliteStatement stmt =
            db.prepare("SELECT paper_hash, clean_title FROM Citations "
                       "WHERE cites = '' OR cited_by = ''");
        if (!stmt.isValid()) {
          qWarning() << "[OpenAlex] Scan: prepare failed";
          return;
        }

        QVector<QPair<QString, QString>> toFetch;
        while (true) {
          auto stepResult = stmt.step();
          if (!stepResult || !*stepResult)
            break;
          toFetch.append(qMakePair(stmt.columnText(0), stmt.columnText(1)));
        }

        if (toFetch.isEmpty()) {
          qInfo() << "[OpenAlex] Scan: no empty citations to fetch";
          return;
        }

        qInfo() << "[OpenAlex] Scan:" << toFetch.size()
                << "empty citation(s) to fetch";

        for (const auto &entry : toFetch) {
          const QString hash = entry.first;
          const QString title = entry.second;
          QMetaObject::invokeMethod(
              this, [this, hash, title]() { fetchAndSave(hash, title); },
              Qt::QueuedConnection);
        }
      },
      Qt::QueuedConnection);
}

/* ── Step 1: find the work ID (semantic → title → author) ───── */

void OpenAlexClient::fetchAndSave(const QString &paperHash,
                                  const QString &title) {
  if (title.trimmed().isEmpty()) {
    qWarning() << "[OpenAlex] Empty title — skipping" << paperHash.left(12);
    return;
  }

  // Load the local metadata (main thread) so the reply handler can pick the
  // right work among the semantic-search candidates.  The search title is the
  // clean_title stored in Citations (identical to @p title — the scan reads it
  // from the same column).
  //
  // Capture meta BY VALUE: this is a QueuedConnection, so the lambda runs
  // later on the main thread after fetchAndSave has returned.  A by-reference
  // capture would dangle on the destroyed local.
  QMetaObject::invokeMethod(
      this,
      [this, paperHash, title]() {
        PaperMeta meta = loadPaperMeta(paperHash);
        searchByTitle(paperHash, meta); // exact-title first (most precise)
      },
      Qt::QueuedConnection);
}

OpenAlexClient::PaperMeta
OpenAlexClient::loadPaperMeta(const QString &paperHash) const {
  PaperMeta meta;
  if (!m_database || !m_database->isOpen())
    return meta;

  auto &db = m_database->db();
  SqliteStatement stmt =
      db.prepare("SELECT title, authors, year, journal FROM Summaries "
                 "WHERE paper_hash = ?");
  if (!stmt.isValid())
    return meta;

  (void)stmt.bind(1, paperHash);
  if (auto step = stmt.step(); step && *step) {
    meta.cleanTitle = stmt.columnText(0).trimmed();
    meta.authors = stmt.columnText(1).split(';', Qt::SkipEmptyParts);
    meta.year = stmt.columnText(2).trimmed().toInt();
    meta.journal = stmt.columnText(3).trimmed();
  }
  return meta;
}

// Fields requested for each search-tier candidate.
static const char *kSearchSelect =
    "id,display_name,authorships,publication_year,primary_location";

void OpenAlexClient::runSearchTier(
    const QString &paperHash, const PaperMeta &meta, const QUrlQuery &query,
    const QString &label,
    std::function<void(const QString &, const PaperMeta &)> fallthrough) {
  if (!m_nam)
    m_nam = new QNetworkAccessManager(this);

  QUrl url("https://api.openalex.org/works");
  url.setQuery(query);

  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::UserAgentHeader, "Hodeia/0.1");

  qInfo() << "[OpenAlex] ──" << label << "──"
          << "paper:" << paperHash.left(12)
          << "| URL:" << url.toString(QUrl::FullyEncoded);

  m_pending = m_nam->get(req);
  connect(
      m_pending, &QNetworkReply::finished, this,
      [this, reply = m_pending, paperHash, meta, label,
       fallthrough = std::move(fallthrough)]() {
        if (m_pending == reply)
          m_pending = nullptr;
        if (scoreAndFetch(reply, paperHash, meta, label))
          return;
        // No confident match — run the next tier, or give up (all tiers
        // failed; leave the columns empty so the scan can retry later,
        // the paper may simply be missing from OpenAlex).
        if (fallthrough)
          fallthrough(paperHash, meta);
        else
          qWarning() << "[OpenAlex] ── NO MATCH ──"
                     << "paper:" << paperHash.left(12)
                     << "| no confident match across semantic, title, author";
      });
}

void OpenAlexClient::semanticSearch(const QString &paperHash,
                                    const PaperMeta &meta) {
  // Tier 2: embedding-based semantic search, used when exact-title search
  // finds no confident match (the right work can be missing from the
  // title-indexed results).  The search text goes directly into
  // "search.semantic" (no separate "search" param); keep apostrophes and
  // hyphens — semantic search embeds the query, so it tolerates punctuation.
  const QString searchTitle = meta.cleanTitle.trimmed();
  if (searchTitle.isEmpty()) {
    qWarning() << "[OpenAlex] Empty title — skipping" << paperHash.left(12);
    return;
  }
  QUrlQuery query;
  query.addQueryItem("search.semantic", searchTitle);
  query.addQueryItem("sort", "relevance_score:desc");
  query.addQueryItem("per-page", "20");
  query.addQueryItem("select", kSearchSelect);
  appendAuthQuery(query);
  runSearchTier(paperHash, meta, query, "SEMANTIC",
                [this](const QString &h, const PaperMeta &m) {
                  searchByAuthor(h, m); // no confident match — tier 3
                });
}

void OpenAlexClient::searchByTitle(const QString &paperHash,
                                   const PaperMeta &meta) {
  // Tier 1: exact-title search (what the OpenAlex website uses) — the most
  // precise signal, so it runs first.  Ranks the matching work high; the
  // author gate then disambiguates duplicates.
  const QString searchTitle = meta.cleanTitle.trimmed();
  if (searchTitle.isEmpty()) {
    qWarning() << "[OpenAlex] Empty title — skipping title search"
               << paperHash.left(12);
    return;
  }
  QUrlQuery query;
  query.addQueryItem("search.title", searchTitle);
  query.addQueryItem("sort", "relevance_score:desc");
  query.addQueryItem("per-page", "20");
  query.addQueryItem("select", kSearchSelect);
  appendAuthQuery(query);
  runSearchTier(paperHash, meta, query, "TITLE",
                [this](const QString &h, const PaperMeta &m) {
                  semanticSearch(h, m); // no confident match — tier 2
                });
}

void OpenAlexClient::searchByAuthor(const QString &paperHash,
                                    const PaperMeta &meta) {
  // Tier 3 (last resort): search by the first author's name.
  // raw_author_name.search is the only author-name filter on /works
  // (author.display_name.search is not a valid field).  bestCandidate then
  // disambiguates.
  const QString author =
      meta.authors.isEmpty() ? QString() : meta.authors.first().trimmed();
  if (author.isEmpty()) {
    qWarning() << "[OpenAlex] No authors for author search"
               << paperHash.left(12);
    return;
  }
  QUrlQuery query;
  query.addQueryItem("filter",
                     QString("raw_author_name.search:%1").arg(author));
  query.addQueryItem("sort", "publication_year:desc");
  query.addQueryItem("per-page", "50");
  query.addQueryItem("select", kSearchSelect);
  appendAuthQuery(query);
  runSearchTier(paperHash, meta, query, "AUTHOR",
                {}); // last tier — no fallback
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
  for (const QChar &c : s.toLower())
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
                                  const PaperMeta &meta) const {
  int bestIdx = -1;
  int bestScore = 0;

  for (int i = 0; i < results.size(); ++i) {
    const QJsonObject w = results.at(i).toObject();
    int score = 0;
    bool authorMatch = false;

    // 1. Title (strongest signal) — fuzzy, kTitleFuzzyPct threshold.
    const QString candTitle = w.value("display_name").toString();
    if (!meta.cleanTitle.isEmpty() &&
        fuzzyMatch(meta.cleanTitle, candTitle, kTitleFuzzyPct))
      score += 3;
    else if (containsCI(candTitle, meta.cleanTitle) ||
             containsCI(meta.cleanTitle, candTitle))
      score += 2;

    // 2. Authors — each local author must fuzzy-match (40% threshold) at
    //    least one candidate author.  We may have fewer authors than
    //    OpenAlex records, so it's enough that all of ours appear.
    if (!meta.authors.isEmpty()) {
      QStringList candAuthors;
      for (const QJsonValue &av : w.value("authorships").toArray())
        if (av.toObject()
                .value("author")
                .toObject()
                .value("display_name")
                .toString()
                .isEmpty() == false)
          candAuthors << av.toObject()
                             .value("author")
                             .toObject()
                             .value("display_name")
                             .toString();
      authorMatch = !candAuthors.isEmpty();
      for (const QString &a : meta.authors) {
        bool found = false;
        for (const QString &ca : candAuthors)
          if (fuzzyMatch(a, ca, kAuthorFuzzyPct)) {
            found = true;
            break;
          }
        if (!found) {
          authorMatch = false;
          break;
        }
      }
      if (authorMatch)
        score += 2;
    }

    // 3. Year — allow ±1 to tolerate online-first vs. issue-date mismatches
    //    (e.g. local says 2004, OpenAlex says 2003 for the same paper).
    if (meta.year > 0) {
      const int candYear = w.value("publication_year").toInt();
      if (qAbs(candYear - meta.year) <= 1)
        score += 1;
    }

    // 4. Journal — fuzzy, kJournalFuzzyPct threshold.
    if (!meta.journal.isEmpty()) {
      const QString candJournal = w.value("primary_location")
                                      .toObject()
                                      .value("source")
                                      .toObject()
                                      .value("display_name")
                                      .toString();
      if (fuzzyMatch(meta.journal, candJournal, kJournalFuzzyPct))
        score += 1;
    }

    // If we have author metadata, a candidate whose authors don't match is
    // likely a *different* paper with a similar title (OpenAlex has many
    // works sharing the same or near-same title by different authors).
    // Reject it outright so we fall through to the next search tier.
    if (!meta.authors.isEmpty() && !authorMatch)
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

/* ── Disambiguation: pick the work ID, then fetch both lists ── */

bool OpenAlexClient::scoreAndFetch(QNetworkReply *reply,
                                   const QString &paperHash,
                                   const PaperMeta &meta,
                                   const QString &label) {
  if (!reply)
    return false;

  if (reply->error() != QNetworkReply::NoError) {
    qWarning()
        << "[OpenAlex] ──" << label << "──"
        << "paper:" << paperHash.left(12) << "| ERROR:" << reply->errorString()
        << "| HTTP:"
        << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    // Transient failure — leave the columns empty so the scan retries.
    return false;
  }

  QByteArray raw = reply->readAll();
  reply->deleteLater();

  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
  if (err.error != QJsonParseError::NoError) {
    qWarning() << "[OpenAlex] ──" << label << "──"
               << "paper:" << paperHash.left(12)
               << "| JSON parse error:" << err.errorString();
    return false;
  }

  QJsonArray results = doc.object().value("results").toArray();
  if (results.isEmpty()) {
    qWarning() << "[OpenAlex] ──" << label << "──"
               << "paper:" << paperHash.left(12) << "| No works returned";
    return false;
  }

  // Disambiguate: pick the candidate that best matches the local metadata.
  const int best = bestCandidate(results, meta);
  if (best < 0) {
    qWarning() << "[OpenAlex] ──" << label << "──"
               << "paper:" << paperHash.left(12) << "| No confident match among"
               << results.size() << "candidates — falling through";
    return false;
  }

  const QJsonObject bestWork = results.at(best).toObject();
  // workId is a URL like "https://openalex.org/W1234567890"
  const QString workId = bestWork.value("id").toString();
  const QString workIdShort = workId.section('/', -1);
  qInfo() << "[OpenAlex] ──" << label << "──"
          << "paper:" << paperHash.left(12) << "| candidates:" << results.size()
          << "| chosen idx:" << best << "| workId:" << workIdShort;

  // Persist the work ID (main thread).
  QMetaObject::invokeMethod(
      this,
      [this, paperHash, workIdShort]() {
        if (!m_database || !m_database->isOpen())
          return;
        auto &db = m_database->db();
        SqliteStatement stmt =
            db.prepare("UPDATE Citations SET work_id = ? WHERE paper_hash = ?");
        if (!stmt.isValid())
          return;
        (void)stmt.bind(1, workIdShort);
        (void)stmt.bind(2, paperHash);
        (void)stmt.exec();
      },
      Qt::QueuedConnection);

  // Start the first list (cites).  The two lists must NOT run in parallel —
  // they share m_listBuffer and m_pending, which a single in-flight request
  // at a time guarantees.  handleListReply chains to cited_by once cites is
  // fully saved.
  fetchWorkList(paperHash, RefKind::Cites, workIdShort);
  return true;
}

/* ── Step 2/3: paginated fetch of a citation list ───────────── */

// Build the URL for one page of a citation list for @p workId.  @p
// filterField is "cites" (works citing W) or "cited_by" (works W cited).
// Sorted most-cited first so the first kMaxWorks kept are the most important.
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

void OpenAlexClient::fetchWorkList(const QString &paperHash, RefKind kind,
                                   const QString &workId) {
  // Column semantics (from the user's standpoint of the queried work W):
  //   "cites"    = the works that CITE W (its citers)
  //   "cited_by" = the works W CITED (its references)
  //
  // OpenAlex /works filter semantics (verified empirically):
  //   cites:{W}    -> works that cite W          (its citers)
  //   cited_by:{W} -> works that W cited         (its references)
  //   (referenced_works:{W} is a synonym for cites:{W} — it also returns
  //    citers, NOT the references, so it must not be used here.)
  const char *filterField = (kind == RefKind::Cites) ? "cites" : "cited_by";
  const char *label = (kind == RefKind::Cites) ? "CITES" : "CITED-BY";

  const QUrl url = buildListUrl(filterField, workId, 1);

  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::UserAgentHeader, "Hodeia/0.1");

  qInfo() << "[OpenAlex] ──" << label << "──"
          << "paper:" << paperHash.left(12) << "| page: 1"
          << "| URL:" << url.toString(QUrl::FullyEncoded);

  // This list is fresh — clear the buffer.  Only one list is ever in flight
  // (scoreAndFetch chains cites → cited_by), so a single buffer is safe.
  m_listBuffer = QJsonArray();

  m_pending = m_nam->get(req);
  connect(m_pending, &QNetworkReply::finished, this,
          [this, reply = m_pending, paperHash, kind, workId]() {
            if (m_pending == reply)
              m_pending = nullptr;
            handleListReply(reply, paperHash, kind, workId, 1);
          });
}

void OpenAlexClient::handleListReply(QNetworkReply *reply,
                                     const QString &paperHash, RefKind kind,
                                     const QString &workId, int page) {
  if (!reply)
    return;

  const char *label = (kind == RefKind::Cites) ? "CITES" : "CITED-BY";

  if (reply->error() != QNetworkReply::NoError) {
    qWarning()
        << "[OpenAlex] ──" << label << "──"
        << "paper:" << paperHash.left(12) << "| page:" << page
        << "| ERROR:" << reply->errorString() << "| HTTP:"
        << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    // Transient failure — leave the column empty so the scan retries.
    return;
  }

  QByteArray raw = reply->readAll();
  reply->deleteLater();

  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
  if (err.error != QJsonParseError::NoError) {
    qWarning() << "[OpenAlex]" << label
               << "JSON parse error:" << err.errorString();
    return;
  }

  const QJsonObject meta = doc.object().value("meta").toObject();
  const int totalCount = meta.value("count").toInt();
  const QJsonArray results = doc.object().value("results").toArray();
  qInfo() << "[OpenAlex] ──" << label << "──"
          << "paper:" << paperHash.left(12) << "| page:" << page
          << "| results:" << results.size() << "| total:" << totalCount;

  // Reduce each result to the fields we store (id, title, authors, year, doi,
  // journal).
  for (const QJsonValue &v : results) {
    const QJsonObject w = v.toObject();
    const QJsonObject loc =
        w.value("primary_location").toObject().value("source").toObject();
    QJsonObject item;
    // id is a URL like "https://openalex.org/W123" — store the short "W123"
    // form (same convention as Citations.work_id).
    const QString workId = w.value("id").toString();
    item["id"] = workId.section('/', -1);
    item["display_name"] = w.value("display_name");
    item["publication_year"] = w.value("publication_year");
    item["doi"] = w.value("doi");
    item["journal"] = loc.value("display_name");

    // authorships: [{ author: { display_name }, ... }, ...]
    // display_name is usually "Family, Given", which itself contains a comma,
    // so join with a delimiter that cannot appear in a name (unit separator).
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
    m_listBuffer.append(item);
  }

  int buffered = static_cast<int>(m_listBuffer.size());
  bool hasMore = buffered < totalCount;
  if (hasMore && buffered >= kMaxWorks) {
    // Reached the cap — keep only the first kMaxWorks (the most-cited, per
    // the sort) and stop paginating.
    qInfo() << "[OpenAlex] ──" << label << "──"
            << "paper:" << paperHash.left(12) << "| capping at" << kMaxWorks
            << "of" << totalCount;
    QJsonArray capped;
    for (int i = 0; i < kMaxWorks; ++i)
      capped.append(m_listBuffer.at(i));
    m_listBuffer = std::move(capped);
    hasMore = false; // we have enough — fall through to save
  }

  if (hasMore) {
    // More pages remain and the cap is not yet reached — fetch the next one.
    const int nextPage = page + 1;
    const char *filterField = (kind == RefKind::Cites) ? "cites" : "cited_by";
    const QUrl url = buildListUrl(filterField, workId, nextPage);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "Hodeia/0.1");

    m_pending = m_nam->get(req);
    connect(m_pending, &QNetworkReply::finished, this,
            [this, reply = m_pending, paperHash, kind, workId, nextPage]() {
              if (m_pending == reply)
                m_pending = nullptr;
              handleListReply(reply, paperHash, kind, workId, nextPage);
            });
    return;
  }

  // Done with this list — save it (main thread).  Then, on the worker thread,
  // start the next list (cites → cited_by) or fire the completion signal.
  // Chaining the *start* (not the save) keeps the worker event loop busy
  // during the whole fetch, so the queued saves are strictly ordered.
  const bool isCites = (kind == RefKind::Cites);
  QString itemsJson =
      m_listBuffer.isEmpty()
          ? QString::fromLatin1(kNoReferencesMsg)
          : QString::fromUtf8(
                QJsonDocument(m_listBuffer).toJson(QJsonDocument::Compact));
  m_listBuffer = QJsonArray();
  const int total = totalCount;

  QMetaObject::invokeMethod(
      this,
      [this, paperHash, kind, itemsJson, total]() {
        saveWorkList(paperHash, kind, itemsJson, total);
      },
      Qt::QueuedConnection);

  if (isCites)
    fetchWorkList(paperHash, RefKind::CitedBy, workId);
  else
    emit referencesSaved(paperHash);
}

/* ── DB save (main thread) ──────────────────────────────────── */

void OpenAlexClient::saveWorkList(const QString &paperHash, RefKind kind,
                                  const QString &itemsJson, int total) {
  if (!m_database || !m_database->isOpen())
    return;

  const char *col = (kind == RefKind::Cites) ? "cites" : "cited_by";
  const char *countCol =
      (kind == RefKind::Cites) ? "cites_count" : "cited_by_count";
  auto &db = m_database->db();
  SqliteStatement stmt = db.prepare(
      QString("UPDATE Citations SET %1 = ?, %2 = ? WHERE paper_hash = ?")
          .arg(QLatin1String(col), QLatin1String(countCol)));
  if (!stmt.isValid())
    return;

  (void)stmt.bind(1, itemsJson);
  (void)stmt.bind(2, total);
  (void)stmt.bind(3, paperHash);
  auto execResult = stmt.exec();
  if (execResult)
    qInfo() << "[OpenAlex] Saved" << itemsJson.size() << "bytes of" << col
            << "(total" << total << ") for" << paperHash.left(12);
}
