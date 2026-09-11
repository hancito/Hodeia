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
// stored list carries each work's author names for a richer citation display.
static const char *kSelectFields =
    "display_name,publication_year,doi,primary_location,authorships";

/* ── Construction / destruction ─────────────────────────────── */

OpenAlexClient::OpenAlexClient(ConfigManager *config, DatabaseManager *database,
                               QObject *parent)
    : QObject(parent), m_config(config), m_database(database) {
  m_thread = new QThread(this);
  moveToThread(m_thread);
  connect(m_thread, &QThread::finished, this, &QObject::deleteLater);
  m_thread->start();
}

OpenAlexClient::~OpenAlexClient() {
  if (m_thread && m_thread->isRunning()) {
    m_thread->quit();
    m_thread->wait(3000);
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

        int count = 0;
        QStringList toFetch;
        while (true) {
          auto stepResult = stmt.step();
          if (!stepResult || !*stepResult)
            break;
          toFetch << stmt.columnText(0) + "|" + stmt.columnText(1);
          count++;
        }

        if (count == 0) {
          qInfo() << "[OpenAlex] Scan: no empty citations to fetch";
          return;
        }

        qInfo() << "[OpenAlex] Scan:" << count << "empty citation(s) to fetch";

        for (const auto &entry : toFetch) {
          int sep = entry.indexOf('|');
          QString hash = entry.left(sep);
          QString title = entry.mid(sep + 1);
          QMetaObject::invokeMethod(
              this, [this, hash, title]() { fetchAndSave(hash, title); },
              Qt::QueuedConnection);
        }
      },
      Qt::QueuedConnection);
}

/* ── Step 1: semantic search for the work ID ────────────────── */

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
        searchForWork(paperHash, title, meta);
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

void OpenAlexClient::searchForWork(const QString &paperHash,
                                   const QString &title,
                                   const PaperMeta &meta) {
  if (!m_nam)
    m_nam = new QNetworkAccessManager(this);

  // The search text goes directly into "search.semantic" (no separate
  // "search" param).  Keep apostrophes and hyphens — semantic search embeds
  // the query, so it tolerates punctuation.
  QString searchTitle = title.trimmed();

  // Pull the top 20 candidates; we disambiguate with title/authors/year/
  // journal (see bestCandidate) because the #1 hit is not always the right
  // work.
  QUrl url("https://api.openalex.org/works");
  QUrlQuery query;
  query.addQueryItem("search.semantic", searchTitle);
  query.addQueryItem("sort", "relevance_score:desc");
  query.addQueryItem("per-page", "20");
  query.addQueryItem(
      "select",
      "id,display_name,authorships,publication_year,primary_location");
  appendAuthQuery(query);
  url.setQuery(query);

  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::UserAgentHeader, "Hodeia/0.1");

  qInfo() << "[OpenAlex] ── SEARCH ──"
          << "paper:" << paperHash.left(12) << "| title:" << searchTitle
          << "| URL:" << url.toString(QUrl::FullyEncoded);

  m_pending = m_nam->get(req);
  connect(m_pending, &QNetworkReply::finished, this,
          [this, reply = m_pending, paperHash, meta]() {
            if (m_pending == reply)
              m_pending = nullptr;
            handleSearchReply(reply, paperHash, meta);
          });
}

/* ── Candidate disambiguation ────────────────────────────────── */

// Normalise a title for comparison: lowercase, drop non-alphanumerics.
static QString normalizeTitle(const QString &s) {
  QString out;
  out.reserve(s.size());
  for (const QChar &c : s.toLower())
    if (c.isLetterOrNumber())
      out.append(c);
  return out;
}

// Case-insensitive containment of a substring (needle) in haystack.
static bool containsCI(const QString &haystack, const QString &needle) {
  const QString n = needle.trimmed();
  if (n.isEmpty())
    return false;
  return haystack.toLower().contains(n.toLower());
}

int OpenAlexClient::bestCandidate(const QJsonArray &results,
                                  const PaperMeta &meta) const {
  const QString nt = normalizeTitle(meta.cleanTitle);
  int bestIdx = -1;
  int bestScore = 0;

  for (int i = 0; i < results.size(); ++i) {
    const QJsonObject w = results.at(i).toObject();
    int score = 0;

    // 1. Title (strongest signal).
    const QString candTitle = w.value("display_name").toString();
    if (!nt.isEmpty() && normalizeTitle(candTitle) == nt)
      score += 3;
    else if (containsCI(candTitle, meta.cleanTitle) ||
             containsCI(meta.cleanTitle, candTitle))
      score += 2;

    // 2. Authors — a candidate matches if its authorships mention every one
    //    of our authors (we may have fewer than OpenAlex records).
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
      const QString candAuthorsAll = candAuthors.join(" | ").toLower();
      bool allFound = !candAuthorsAll.isEmpty();
      for (const QString &a : meta.authors)
        if (!containsCI(candAuthorsAll, a)) {
          allFound = false;
          break;
        }
      if (allFound)
        score += 2;
    }

    // 3. Year.
    if (meta.year > 0) {
      const int candYear = w.value("publication_year").toInt();
      if (candYear == meta.year)
        score += 1;
    }

    // 4. Journal — from primary_location.source.display_name.
    if (!meta.journal.isEmpty()) {
      const QString candJournal = w.value("primary_location")
                                      .toObject()
                                      .value("source")
                                      .toObject()
                                      .value("display_name")
                                      .toString();
      if (containsCI(candJournal, meta.journal) ||
          containsCI(meta.journal, candJournal))
        score += 1;
    }

    if (score > bestScore) {
      bestScore = score;
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

/* ── Step 1 response: pick the work ID, then fetch both lists ── */

void OpenAlexClient::handleSearchReply(QNetworkReply *reply,
                                       const QString &paperHash,
                                       const PaperMeta &meta) {
  if (!reply)
    return;

  if (reply->error() != QNetworkReply::NoError) {
    qWarning()
        << "[OpenAlex] ── SEARCH ──"
        << "paper:" << paperHash.left(12) << "| ERROR:" << reply->errorString()
        << "| HTTP:"
        << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    // Transient failure — leave the columns empty so the scan retries.
    return;
  }

  QByteArray raw = reply->readAll();
  reply->deleteLater();

  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
  if (err.error != QJsonParseError::NoError) {
    qWarning() << "[OpenAlex] Search JSON parse error:" << err.errorString();
    return;
  }

  QJsonArray results = doc.object().value("results").toArray();
  if (results.isEmpty()) {
    qWarning() << "[OpenAlex] ── SEARCH ──"
               << "paper:" << paperHash.left(12) << "| No work found for title";
    QMetaObject::invokeMethod(
        this,
        [this, paperHash]() {
          saveWorkList(paperHash, RefKind::Cites, kNoReferencesMsg, 0);
          saveWorkList(paperHash, RefKind::CitedBy, kNoReferencesMsg, 0);
        },
        Qt::QueuedConnection);
    return;
  }

  // Disambiguate: pick the candidate that best matches the local metadata.
  const int best = bestCandidate(results, meta);
  if (best < 0) {
    qWarning() << "[OpenAlex] ── SEARCH ──"
               << "paper:" << paperHash.left(12) << "| No confident match among"
               << results.size()
               << "candidates — "
                  "leaving empty so the scan can retry";
    // No marker: a confident mismatch means we simply haven't found it yet.
    return;
  }

  const QJsonObject bestWork = results.at(best).toObject();
  // workId is a URL like "https://openalex.org/W1234567890"
  const QString workId = bestWork.value("id").toString();
  const QString workIdShort = workId.section('/', -1);
  qInfo() << "[OpenAlex] ── SEARCH ──"
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

  // Fetch the two lists in sequence (cites, then cited_by).  Each is a
  // self-contained paginated fetch; the last one to finish emits the signal.
  fetchWorkList(paperHash, RefKind::Cites, workIdShort);
  fetchWorkList(paperHash, RefKind::CitedBy, workIdShort);
}

/* ── Step 2/3: paginated fetch of a citation list ───────────── */

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

  QUrl url("https://api.openalex.org/works");
  QUrlQuery query;
  query.addQueryItem("filter", QString("%1:%2").arg(filterField, workId));
  query.addQueryItem("select", kSelectFields);
  // Most-cited first, so the first kMaxWorks we keep are the most important.
  query.addQueryItem("sort", "cited_by_count:desc");
  appendPage(query, 1);
  appendAuthQuery(query);
  url.setQuery(query);

  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::UserAgentHeader, "Hodeia/0.1");

  qInfo() << "[OpenAlex] ──" << label << "──"
          << "paper:" << paperHash.left(12) << "| page: 1"
          << "| URL:" << url.toString(QUrl::FullyEncoded);

  // This list is fresh — clear the buffer.  (The two fetches run back-to-back
  // from the same call site, so reset before each.)
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

  // Reduce each result to the fields we store (title, authors, year, doi,
  // journal).
  for (const QJsonValue &v : results) {
    const QJsonObject w = v.toObject();
    const QJsonObject loc =
        w.value("primary_location").toObject().value("source").toObject();
    QJsonObject item;
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

  const bool hasMore = static_cast<int>(m_listBuffer.size()) < totalCount;
  if (hasMore && static_cast<int>(m_listBuffer.size()) >= kMaxWorks) {
    // Reached the cap — keep only the first kMaxWorks and stop paginating.
    qInfo() << "[OpenAlex] ──" << label << "──"
            << "paper:" << paperHash.left(12) << "| capping at" << kMaxWorks
            << "of" << totalCount;
    while (m_listBuffer.size() > kMaxWorks)
      m_listBuffer.removeLast();
  } else if (hasMore) {
    // More pages remain and the cap is not yet reached — fetch the next one.
    // Filter direction must match fetchWorkList: cites:{W} = citers,
    // cited_by:{W} = references.
    const int nextPage = page + 1;
    const char *filterField = (kind == RefKind::Cites) ? "cites" : "cited_by";

    QUrl url("https://api.openalex.org/works");
    QUrlQuery query;
    query.addQueryItem("filter", QString("%1:%2").arg(filterField, workId));
    query.addQueryItem("select", kSelectFields);
    query.addQueryItem("sort", "cited_by_count:desc");
    appendPage(query, nextPage);
    appendAuthQuery(query);
    url.setQuery(query);

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

  // Done — save the accumulated list (main thread) and, for cited_by, fire
  // the completion signal.
  QString itemsJson =
      m_listBuffer.isEmpty()
          ? QString::fromLatin1(kNoReferencesMsg)
          : QString::fromUtf8(
                QJsonDocument(m_listBuffer).toJson(QJsonDocument::Compact));
  m_listBuffer = QJsonArray();

  QMetaObject::invokeMethod(
      this,
      [this, paperHash, kind, itemsJson, total = totalCount]() {
        saveWorkList(paperHash, kind, itemsJson, total);
        if (kind == RefKind::CitedBy)
          emit referencesSaved(paperHash);
      },
      Qt::QueuedConnection);
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
            << "(total" << total << ")for" << paperHash.left(12);
}
