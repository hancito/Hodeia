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

// Fields requested for each cited/citing work.  primary_location is selected
// whole (nested paths are not valid in select); the journal is read from
// primary_location.source.display_name.
static const char *kSelectFields =
    "display_name,publication_year,doi,primary_location";

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

  if (!m_nam)
    m_nam = new QNetworkAccessManager(this);

  // The search text goes directly into "search.semantic" (no separate
  // "search" param).  Keep apostrophes and hyphens — semantic search embeds
  // the query, so it tolerates punctuation.
  QString searchTitle = title.trimmed();

  QUrl url("https://api.openalex.org/works");
  QUrlQuery query;
  query.addQueryItem("search.semantic", searchTitle);
  query.addQueryItem("sort", "relevance_score:desc");
  query.addQueryItem("per-page", "1");
  query.addQueryItem("select", "id");
  appendAuthQuery(query);
  url.setQuery(query);

  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::UserAgentHeader, "Hodeia/0.1");

  qInfo() << "[OpenAlex] ── SEARCH ──"
          << "paper:" << paperHash.left(12) << "| title:" << searchTitle
          << "| URL:" << url.toString(QUrl::FullyEncoded);

  m_pending = m_nam->get(req);
  connect(m_pending, &QNetworkReply::finished, this,
          [this, reply = m_pending, paperHash]() {
            if (m_pending == reply)
              m_pending = nullptr;
            handleSearchReply(reply, paperHash);
          });
}

/* ── Step 1 response: store work ID, then fetch both lists ──── */

void OpenAlexClient::handleSearchReply(QNetworkReply *reply,
                                       const QString &paperHash) {
  if (!reply)
    return;

  if (reply->error() != QNetworkReply::NoError) {
    qWarning()
        << "[OpenAlex] ── SEARCH ──"
        << "paper:" << paperHash.left(12) << "| ERROR:" << reply->errorString()
        << "| HTTP:"
        << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
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
          saveWorkList(paperHash, RefKind::Cites, kNoReferencesMsg);
          saveWorkList(paperHash, RefKind::CitedBy, kNoReferencesMsg);
        },
        Qt::QueuedConnection);
    return;
  }

  // workId is a URL like "https://openalex.org/W1234567890"
  QString workId = results.first().toObject().value("id").toString();
  const QString workIdShort = workId.section('/', -1);
  qInfo() << "[OpenAlex] ── SEARCH ──"
          << "paper:" << paperHash.left(12) << "| workId:" << workIdShort;

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
  // "cites" column  = works this paper cited  -> filter=referenced_works:{W}
  // "cited_by" col  = works that cited it     -> filter=cites:{W}
  // (OpenAlex's "cites:" filter means "works that cite", i.e. the reverse.)
  const char *filterField =
      (kind == RefKind::Cites) ? "referenced_works" : "cites";
  const char *label = (kind == RefKind::Cites) ? "CITES" : "CITED-BY";

  QUrl url("https://api.openalex.org/works");
  QUrlQuery query;
  query.addQueryItem("filter", QString("%1:%2").arg(filterField, workId));
  query.addQueryItem("select", kSelectFields);
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

  // Reduce each result to the fields we store (title, year, doi, journal).
  for (const QJsonValue &v : results) {
    const QJsonObject w = v.toObject();
    const QJsonObject loc =
        w.value("primary_location").toObject().value("source").toObject();
    QJsonObject item;
    item["display_name"] = w.value("display_name");
    item["publication_year"] = w.value("publication_year");
    item["doi"] = w.value("doi");
    item["journal"] = loc.value("display_name");
    m_listBuffer.append(item);
  }

  if (static_cast<int>(m_listBuffer.size()) < totalCount) {
    // More pages remain — fetch the next one.
    const int nextPage = page + 1;
    const char *filterField =
        (kind == RefKind::Cites) ? "referenced_works" : "cites";

    QUrl url("https://api.openalex.org/works");
    QUrlQuery query;
    query.addQueryItem("filter", QString("%1:%2").arg(filterField, workId));
    query.addQueryItem("select", kSelectFields);
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
      [this, paperHash, kind, itemsJson]() {
        saveWorkList(paperHash, kind, itemsJson);
        if (kind == RefKind::CitedBy)
          emit referencesSaved(paperHash);
      },
      Qt::QueuedConnection);
}

/* ── DB save (main thread) ──────────────────────────────────── */

void OpenAlexClient::saveWorkList(const QString &paperHash, RefKind kind,
                                  const QString &itemsJson) {
  if (!m_database || !m_database->isOpen())
    return;

  const char *col = (kind == RefKind::Cites) ? "cites" : "cited_by";
  auto &db = m_database->db();
  SqliteStatement stmt =
      db.prepare(QString("UPDATE Citations SET %1 = ? WHERE paper_hash = ?")
                     .arg(QLatin1String(col)));
  if (!stmt.isValid())
    return;

  (void)stmt.bind(1, itemsJson);
  (void)stmt.bind(2, paperHash);
  auto execResult = stmt.exec();
  if (execResult)
    qInfo() << "[OpenAlex] Saved" << itemsJson.size() << "bytes of" << col
            << "for" << paperHash.left(12);
}
