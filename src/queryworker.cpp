#include "queryworker.h"
#include "configmanager.h"
#include "database.h"
#include "localbackend.h"
#include "remotebackend.h"
#include "sqlitevec.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>

/* ── Prompts ───────────────────────────────────────────────────── */

static const QString s_systemPrompt = []() {
  return R"(You are an expert academic research assistant.
You are provided with highly relevant pages extracted from multiple research papers.
Each page is presented with its image (when available) followed by its extracted text.
Synthesize a cohesive, detailled, comprehensive response answering the user's query utilizing both
the textual evidence and visual figures/tables/charts provided. Cite sources explicitly.
When you cite the context use a clean academic citation.
Use LaTeX for mathematics.
Output the response in Markdown.)";
}();

/* ── Construction / destruction ─────────────────────────────────── */

QueryWorker::QueryWorker(ConfigManager *config, DatabaseManager *database,
                         QObject *parent)
    : QObject(parent), m_config(config), m_database(database) {
  m_chatBackend = new LocalBackend(this);
  m_remoteBackend = new RemoteBackend(this);
}

QueryWorker::~QueryWorker() = default;

/* ── Public API ─────────────────────────────────────────────────── */

void QueryWorker::executeQuery(const QString &queryText, int maxPages) {
  emit started();
  if (queryText.trimmed().isEmpty()) {
    emit queryFinished(QString(), false, "Empty query");
    return;
  }

  qDebug() << "[QueryWorker] Starting query:" << queryText.left(60)
           << "| maxPages:" << maxPages;

  QElapsedTimer totalTimer;
  totalTimer.start();

  // 1. Retrieve & group pages from vector search
  QList<PageContext> pages = retrieveAndGroupPages(queryText, maxPages);
  if (pages.isEmpty()) {
    emit queryFinished(QString(), false,
                       "No relevant information found in the library.");
    return;
  }

  // 2. Build citations footer
  QStringList citations = buildCitationsFooter(pages);

  // 3. Resolve backend
  BackendInfo backend = resolveBackend();
  qDebug() << "[QueryWorker] Backend:" << (backend.isLocal ? "local" : "remote")
           << "| Model:" << backend.modelName;

  bool wasEmbeddingReady = prepareGpu(backend.isLocal);

  // 4. Build prompt
  PromptPayload payload = buildPrompt(pages, queryText);
  qDebug() << "[QueryWorker] Prompt built — text context:"
           << payload.textContext.length()
           << "chars, visuals:" << payload.visualContext.size();

  // 5. Run inference
  QString result;
  if (backend.isLocal)
    result = queryLocal(payload, backend);
  else
    result = queryRemote(payload, backend);

  if (result.isEmpty()) {
    QString msg = "Empty response from model";
    emit queryFinished(QString(), false, msg);
    qDebug() << "[QueryWorker] ✗ Query failed:" << msg;
  } else {
    // Append sources footer
    if (!citations.isEmpty()) {
      result += "\n\n**Sources Consulted:**\n" + citations.join("\n");
    }
    saveQuery(queryText, result, backend.modelName);
    emit queryFinished(result, true, QString());
    qDebug() << "[QueryWorker] ✓ Query answered (" << result.length()
             << "chars) in" << totalTimer.elapsed() << "ms";
  }

  cleanupGpu(backend.isLocal, wasEmbeddingReady);
}

/* ── Backend resolution ─────────────────────────────────────────── */

QueryWorker::BackendInfo QueryWorker::resolveBackend() {
  BackendInfo info;
  info.isLocal = (m_config->activeBackend() == "Llama.cpp");
  info.modelName = m_config->activeModel();
  info.config = m_config->backendConfig(m_config->activeBackend());

  if (info.isLocal) {
    QString folderPath = m_config->summarizersPath() + "/" + info.modelName;
    QStringList ggufs =
        QDir(folderPath)
            .entryList(QStringList{"*.gguf"}, QDir::Files | QDir::NoSymLinks);
    for (const QString &f : ggufs) {
      if (!f.contains("mmproj", Qt::CaseInsensitive)) {
        info.modelPath = folderPath + "/" + f;
        break;
      }
    }
  }
  return info;
}

/* ── GPU preparation ────────────────────────────────────────────── */

bool QueryWorker::prepareGpu(bool isLocal) {
  if (!isLocal || !m_database)
    return false;
  // Keep embedding model loaded — vector search already completed.
  return false;
}

void QueryWorker::cleanupGpu(bool isLocal, bool wasEmbeddingReady) {
  if (wasEmbeddingReady && m_database) {
    qDebug() << "[QueryWorker] Reloading embedding model...";
    QElapsedTimer t;
    t.start();
    m_database->vecManager().initialize();
    qDebug() << "[QueryWorker] Embedding model reloaded in" << t.elapsed()
             << "ms";
  }
  if (isLocal) {
    m_chatBackend->unloadModel();
    qDebug() << "[QueryWorker] Chat model unloaded";
  }
}

/* ── Local query ────────────────────────────────────────────────── */

QString QueryWorker::queryLocal(const PromptPayload &payload,
                                const BackendInfo &backend) {
  uint64_t payloadChars = payload.textContext.length() +
                          payload.systemPrompt.length() +
                          payload.userMessage.length();
  uint32_t estimatedCtxTokens = static_cast<uint32_t>(payloadChars / 3) + 4096;

  qDebug() << "[QueryWorker] Loading local model:" << backend.modelPath
           << "| estimatedCtxTokens:" << estimatedCtxTokens;
  QElapsedTimer loadTimer;
  loadTimer.start();

  auto loadResult =
      m_chatBackend->loadModel(backend.modelPath, estimatedCtxTokens);
  if (!loadResult) {
    qWarning() << "[QueryWorker] Failed to load model:" << loadResult.error();
    return QString();
  }
  qDebug() << "[QueryWorker] Model loaded in" << loadTimer.elapsed() << "ms";

  QElapsedTimer genTimer;
  genTimer.start();
  QString result = m_chatBackend->summarize(payload);
  qDebug() << "[QueryWorker] Generation completed in" << genTimer.elapsed()
           << "ms"
           << "(" << result.length() << "chars)";
  return result;
}

/* ── Remote query ───────────────────────────────────────────────── */

QString QueryWorker::queryRemote(const PromptPayload &payload,
                                 const BackendInfo &backend) {
  QString apiUrl = backend.config.value("url").toString();
  QString apiKey = backend.config.value("api_key").toString();

  m_remoteBackend->setApiUrl(apiUrl);
  m_remoteBackend->setApiKey(apiKey);
  m_remoteBackend->setModelName(backend.modelName);

  qDebug() << "[QueryWorker] Remote request — URL:" << apiUrl
           << "| Model:" << backend.modelName << "| Visual context:"
           << (payload.visualContext.empty() ? "disabled" : "enabled");

  QElapsedTimer genTimer;
  genTimer.start();

  QString result, error;
  QEventLoop loop;
  QObject::connect(
      m_remoteBackend, &RemoteBackend::finished, this,
      [&result, &error, &loop](const QString &text, const QString &err) {
        result = text;
        error = err;
        loop.quit();
      },
      Qt::SingleShotConnection);

  if (!payload.pageBlocks.empty()) {
    m_remoteBackend->generateInterleaved(payload.systemPrompt, payload,
                                         !payload.visualContext.empty());
  } else if (payload.visualContext.empty()) {
    m_remoteBackend->generate(payload.systemPrompt, payload.userMessage +
                                                        "\n\n" +
                                                        payload.textContext);
  } else {
    m_remoteBackend->generate(payload.systemPrompt,
                              payload.userMessage + "\n\n" +
                                  payload.textContext,
                              payload.visualContext);
  }

  loop.exec(QEventLoop::ExcludeUserInputEvents);

  qDebug() << "[QueryWorker] Remote generation completed in"
           << genTimer.elapsed() << "ms"
           << "(" << result.length() << "chars)";

  if (!error.isEmpty())
    return QString();
  return result;
}

/* ── Retrieval & page grouping ──────────────────────────────────── */

QList<PageContext> QueryWorker::retrieveAndGroupPages(const QString &queryText,
                                                      int maxPages) const {
  if (!m_database || !m_database->isOpen()) {
    qWarning() << "[QueryWorker] database not open";
    return {};
  }

  // Fetch more chunks than maxPages so we have enough after dedup.
  int k = qMax(maxPages * 4, 80);
  QList<ChunkSearchResult> chunks =
      m_database->vecManager().search(m_database->db(), queryText, k);

  if (chunks.isEmpty()) {
    qDebug() << "[QueryWorker] no chunks found for query";
    return {};
  }

  // ── Group and deduplicate by (paperHash, page) ──────────────────
  // Key: "hash|page"  →  index into the ordered result list
  QMap<QString, int> pageKeyToIndex;
  QList<PageContext> pages;

  // Image directory for resolving page PNGs.
  QString imgPath = m_config->projectImgPath(m_config->lastProject());
  QDir imgDir(imgPath);

  int totalChars = 0;

  for (const auto &c : chunks) {
    if (pages.size() >= static_cast<qsizetype>(maxPages))
      break;

    QString key = c.paperHash + "|" + QString::number(c.page);

    auto it = pageKeyToIndex.find(key);
    if (it != pageKeyToIndex.end()) {
      // Existing page — append chunk text if unique.
      PageContext &pc = pages[it.value()];
      if (!pc.textChunks.contains(c.text))
        pc.textChunks.append(c.text);
      continue;
    }

    PageContext pc;
    pc.paperHash = c.paperHash;
    pc.page = c.page;
    pc.textChunks.append(c.text);
    pc.cleanTitle = c.cleanTitle;
    pc.citation = c.citation;

    // Resolve image path: hash_page_NNN.png
    if (imgDir.exists()) {
      QString imgFile = QString("%1_page_%2.png")
                            .arg(c.paperHash)
                            .arg(pc.page, 3, 10, QChar('0'));
      QString absoluteImg = imgDir.absoluteFilePath(imgFile);
      if (QFile::exists(absoluteImg))
        pc.imageFile = absoluteImg;
    }

    int idx = pages.size();
    pageKeyToIndex.insert(key, idx);
    pages.append(pc);
    totalChars += c.text.length();
  }

  qDebug() << "[QueryWorker] retrieved" << chunks.size()
           << "chunks → grouped into" << pages.size() << "unique pages ("
           << totalChars << "chars)";
  return pages;
}

/* ── Citations footer ───────────────────────────────────────────── */

QStringList
QueryWorker::buildCitationsFooter(const QList<PageContext> &pages) const {
  // Group by source string, collect page numbers.
  QMap<QString, QSet<int>> sourcePages;

  for (const auto &pc : pages) {
    QString src;
    if (!pc.cleanTitle.isEmpty()) {
      src = pc.citation.isEmpty() ? pc.cleanTitle
                                  : pc.citation + ": " + pc.cleanTitle;
    } else {
      src = pc.paperHash.left(12) + "...";
    }
    sourcePages[src].insert(pc.page);
  }

  QStringList citations;
  for (auto it = sourcePages.constBegin(); it != sourcePages.constEnd(); ++it) {
    QString src = it.key();
    QList<int> sortedPages = it.value().values();
    std::sort(sortedPages.begin(), sortedPages.end());
    QString pagesStr = QString();
    for (int i = 0; i < sortedPages.size(); ++i) {
      if (i > 0)
        pagesStr += ", ";
      pagesStr += QString::number(sortedPages[i]);
    }
    citations.append("- " + src + " (Pages: " + pagesStr + ")");
  }

  return citations;
}

/* ── Prompt building ────────────────────────────────────────────── */

PromptPayload QueryWorker::buildPrompt(const QList<PageContext> &pages,
                                       const QString &queryText) const {
  PromptPayload payload;
  payload.systemPrompt = s_systemPrompt;

  // Build structured text context: one block per page.
  QStringList blocks;
  for (int i = 0; i < pages.size(); ++i) {
    const PageContext &pc = pages[i];
    QString src =
        pc.cleanTitle.isEmpty()
            ? pc.paperHash.left(12) + "..."
            : (pc.citation.isEmpty() ? pc.cleanTitle
                                     : pc.citation + ": " + pc.cleanTitle);

    QString block = QString("=== PAGE CONTEXT %1 (%2 - Page %3) ===\n")
                        .arg(i + 1)
                        .arg(src)
                        .arg(pc.page);

    for (const QString &chunk : pc.textChunks)
      block += chunk + "\n[---]\n";

    block += "Citation: " + pc.citation + "\n\n";
    blocks.append(block);

    // Build PageBlock with image
    PageBlock pb;
    pb.pageNumber = i + 1;
    pb.text = block;

    if (m_config->useVisualContext() && !pc.imageFile.isEmpty()) {
      QImage img(pc.imageFile);
      if (!img.isNull())
        pb.visualContext = PageImage::fromQImage(img);
    }
    payload.pageBlocks.push_back(std::move(pb));
  }

  payload.textContext = blocks.join("\n");
  payload.userMessage = "Core Task Query: " + queryText +
                        "\n\nSynthesize your final cross-paper response now:";

  // Load page images for multimodal backends (legacy).
  if (m_config->useVisualContext()) {
    for (const auto &pc : pages) {
      if (pc.imageFile.isEmpty())
        continue;
      QImage img(pc.imageFile);
      if (img.isNull())
        continue;
      payload.visualContext.push_back(PageImage::fromQImage(img));
    }
    qDebug() << "[QueryWorker] Visual context —" << payload.visualContext.size()
             << "image(s) loaded";
  }

  return payload;
}

const QString &QueryWorker::systemPrompt() { return s_systemPrompt; }

void QueryWorker::stop() {
  m_stopping = true;
  m_remoteBackend->cancel();
  m_chatBackend->unloadModel();
  emit stopped();
  qDebug() << "[QueryWorker] Stop requested";
}

/* ── Database persistence ───────────────────────────────────────── */

void QueryWorker::saveQuery(const QString &queryText, const QString &response,
                            const QString &modelName) {
  if (!m_database || !m_database->isOpen()) {
    qWarning() << "[QueryWorker] Database not open — cannot save query";
    return;
  }

  auto &db = m_database->db();

  SqliteStatement stmt =
      db.prepare("INSERT INTO Queries (query, response, llmodel, date) "
                 "VALUES (?, ?, ?, ?)");
  if (!stmt.isValid()) {
    qCritical() << "[QueryWorker] Failed to prepare query insert";
    return;
  }

  (void)stmt.bind(1, queryText);
  (void)stmt.bind(2, response);
  (void)stmt.bind(3, modelName);
  (void)stmt.bind(4, QDateTime::currentDateTimeUtc());

  auto execResult = stmt.exec();
  if (!execResult) {
    qCritical() << "[QueryWorker] Failed to save query:" << execResult.error();
    return;
  }

  if (m_database->queryModel())
    m_database->queryModel()->loadAll();

  qDebug() << "[QueryWorker] Saved query response (" << response.length()
           << "chars)";
}
