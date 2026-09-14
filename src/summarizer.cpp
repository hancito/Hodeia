#include "summarizer.h"
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
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>

/* ── Prompts ───────────────────────────────────────────────────── */

static const QString s_systemPromptVisual = []() {
  return R"(You are Senior Research Analyst. Your task is to analyze academic papers with extreme precision. When asked to summarize a paper you will:
  1. The document is provided page by page. Each page's image (when available) precedes its extracted text.
  2. Use the images to gather broader context and verify data, tables, and figures alongside the OCR text.
  3. And respond with high details following this structured schema in english and don't put icons:
  4. Use mathematical description when pertinent
{
  "title": "The full title of the paper",
  "authors": [A list of author names],
  "journal": "journal or conference name",
  "year": "The publication year as an integer",
  "topics": [A list the 5 most relevant topics or keywords from the paper.],
  "researchObjective": "Identify the primary question or hypothesis in 1 sentence",
  "methodology": "Describe the study design, sample size, and key variables. Use a paragraph",
  "coreFindingsAndSignificance": ["Highlight the most statistically or theoretically significant results. Use a bulleted list"],
  "criticalLimitations": ["Identify a list of internal or external validity threats mentioned or observed.",]
  "citableSummary": "A two-sentence summary suitable for a literature review"
}
Use latex format for mathematics. Don't use markdown formating. Return only valid JSON.)";
}();

static const QString s_systemPromptText = []() {
  return R"(You are Senior Research Analyst. Your task is to analyze academic papers with extreme precision. When asked to summarize a paper you will:
  1. Read the provided text context extracted from the paper, organized page by page.
  2. And respond with high details following this structured schema in english and don't put icons:
  3. Use mathematical description when pertinent
{
  "title": "The full title of the paper",
  "authors": [A list of author names],
  "journal": "journal or conference name",
  "year": "The publication year as an integer",
  "topics": [A list the 5 most relevant topics or keywords from the paper.],
  "researchObjective": "Identify the primary question or hypothesis in 1 sentence",
  "methodology": "Describe the study design, sample size, and key variables. Use a paragraph",
  "coreFindingsAndSignificance": ["Highlight the most statistically or theoretically significant results. Use a bulleted list"],
  "criticalLimitations": ["Identify a list of internal or external validity threats mentioned or observed.",]
  "citableSummary": "A two-sentence summary suitable for a literature review"
}
Use latex format for mathematics. Don't use markdown formating. Return only valid JSON.)";
}();

static const QString s_userPrompt = []() {
  return R"(Summarize this paper:)";
}();

const QString &Summarizer::systemPrompt() { return s_systemPromptVisual; }
const QString &Summarizer::userPrompt() { return s_userPrompt; }

/* ── Construction / destruction ─────────────────────────────────── */

Summarizer::Summarizer(ConfigManager *config, DatabaseManager *database,
                       QObject *parent)
    : QObject(parent), m_config(config), m_database(database) {
  m_chatBackend = new LocalBackend(this);
  m_remoteBackend = new RemoteBackend(this);
}

Summarizer::~Summarizer() = default;

/* ── Public API ─────────────────────────────────────────────────── */

void Summarizer::summarizePapers(const QStringList &hashes) {
  emit started();
  if (hashes.isEmpty())
    return;

  // Clear any cancellation left over from a previous, abandoned run.
  m_cancelled = false;

  // Mark a DB operation as in flight so DatabaseManager::switchDatabase()
  // refuses to close the shared connection while we are using it.
  if (m_database)
    m_database->beginDbOperation();

  qDebug() << "[Summarizer] Starting summarization for" << hashes.size()
           << "paper(s)";

  QElapsedTimer totalTimer;
  totalTimer.start();

  BackendInfo backend = resolveBackend();
  qDebug() << "[Summarizer] Backend:" << (backend.isLocal ? "local" : "remote")
           << "| Model:" << backend.modelName;

  bool wasEmbeddingReady = prepareGpu(backend.isLocal);

  int successCount = 0;
  for (int i = 0; i < hashes.size(); ++i) {
    // Process pending events so that a queued stop() is handled.
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 100);
    if (isStopping())
      break;

    const QString &hash = hashes[i];
    qDebug() << "[Summarizer] Processing paper" << i + 1 << "/" << hashes.size()
             << "—" << hash;

    PromptPayload payload = buildPrompt(hash);
    qDebug() << "[Summarizer] Prompt built — text context:"
             << payload.textContext.length()
             << "chars, visuals:" << payload.visualContext.size();

    QString result;
    if (backend.isLocal)
      result = summarizeLocal(payload, backend);
    else
      result = summarizeRemote(payload, backend);

    if (m_cancelled) {
      // stop() fired mid-inference. Do NOT persist a partial summary and stop
      // processing further papers.
      emit paperFinished(hash, false, "Cancelled");
      qDebug() << "[Summarizer] ✗ Paper" << hash << "cancelled — no DB write";
      break;
    }

    if (result.isEmpty()) {
      QString msg = "Empty response";
      emit paperFinished(hash, false, msg);
      qDebug() << "[Summarizer] ✗ Paper" << hash << "failed:" << msg;
    } else {
      PaperSummary summary = Helpers::parseSummaryJson(result);
      saveSummary(hash, summary, result, backend.modelName);

      successCount++;
      emit paperFinished(hash, true,
                         "Summary generated and saved (" +
                             QString::number(result.length()) + " chars)");
      qDebug() << "[Summarizer] ✓ Paper" << hash << "summarized and saved"
               << "(" << result.length() << "chars)";
    }
  }

  emit allFinished(successCount, hashes.size());
  qDebug() << "[Summarizer] All done —" << successCount << "/" << hashes.size()
           << "succeeded in" << totalTimer.elapsed() << "ms";

  cleanupGpu(backend.isLocal, wasEmbeddingReady);
  if (m_database)
    m_database->endDbOperation();
}

/* ── Backend resolution ─────────────────────────────────────────── */

Summarizer::BackendInfo Summarizer::resolveBackend() {
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

bool Summarizer::prepareGpu(bool isLocal) {
  if (!isLocal || !m_database)
    return false;

  SqliteVecManager &vecMgr = m_database->vecManager();
  if (vecMgr.isReady()) {
    vecMgr.shutdown();
    qDebug() << "[Summarizer] Embedding model unloaded to free GPU memory";
    return true;
  }
  return false;
}

void Summarizer::cleanupGpu(bool isLocal, bool wasEmbeddingReady) {
  if (wasEmbeddingReady && m_database) {
    qDebug() << "[Summarizer] Reloading embedding model...";
    QElapsedTimer t;
    t.start();
    m_database->vecManager().initialize();
    qDebug() << "[Summarizer] Embedding model reloaded in" << t.elapsed()
             << "ms";
  }
  if (isLocal) {
    m_chatBackend->unloadModel();
    qDebug() << "[Summarizer] Chat model unloaded";
  }
}

/* ── Local summarization ────────────────────────────────────────── */

QString Summarizer::summarizeLocal(const PromptPayload &payload,
                                   const BackendInfo &backend) {
  // Estimate the token budget the upcoming prompt will need.
  // ~3 chars per token is a conservative upper bound for English text.
  // We add 4096 tokens of headroom for the generated response.
  uint64_t payloadChars = payload.textContext.length() +
                          payload.systemPrompt.length() +
                          payload.userMessage.length();
  uint32_t estimatedCtxTokens = static_cast<uint32_t>(payloadChars / 3) + 4096;

  qDebug() << "[Summarizer] Loading local model:" << backend.modelPath
           << "| estimatedCtxTokens:" << estimatedCtxTokens;
  QElapsedTimer loadTimer;
  loadTimer.start();

  auto loadResult =
      m_chatBackend->loadModel(backend.modelPath, estimatedCtxTokens);
  if (!loadResult) {
    qWarning() << "[Summarizer] Failed to load model:" << loadResult.error();
    return QString();
  }
  qDebug() << "[Summarizer] Model loaded in" << loadTimer.elapsed() << "ms";

  QElapsedTimer genTimer;
  genTimer.start();
  // Pass the cancel flag so a queued stop() can interrupt a long local
  // generation at the next token boundary (see InferenceEngine::generate).
  QString result = m_chatBackend->summarize(payload, &m_cancelled);
  qDebug() << "[Summarizer] Generation completed in" << genTimer.elapsed()
           << "ms"
           << "(" << result.length() << "chars)"
           << (m_cancelled ? "— CANCELLED" : "");
  return result;
}

/* ── Remote summarization ───────────────────────────────────────── */

QString Summarizer::summarizeRemote(const PromptPayload &payload,
                                    const BackendInfo &backend) {

  QString apiUrl = backend.config.value("url").toString();
  QString apiKey = backend.config.value("api_key").toString();

  m_remoteBackend->setApiUrl(apiUrl);
  m_remoteBackend->setApiKey(apiKey);
  m_remoteBackend->setModelName(backend.modelName);
  m_remoteBackend->setForceJson(true);

  // Count images across page blocks
  int imageCount = 0;
  for (const auto &pb : payload.pageBlocks)
    if (!pb.visualContext.data.empty())
      imageCount++;

  qDebug() << "[Summarizer] Remote request — URL:" << apiUrl
           << "| Model:" << backend.modelName << "| images:" << imageCount;

  QElapsedTimer genTimer;
  genTimer.start();

  // Synchronous wait for the async remote call.
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
                                         imageCount > 0);
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

  qDebug() << "[Summarizer] Remote generation completed in"
           << genTimer.elapsed() << "ms"
           << "(" << result.length() << "chars)";

  if (!error.isEmpty())
    return QString();
  return result;
}

/* ── Prompt building ────────────────────────────────────────────── */

QString Summarizer::retrieveContext(const QString &paperHash) const {
  if (!m_database || !m_database->isOpen()) {
    qWarning() << "[Summarizer] database not open";
    return QString();
  }

  QList<ChunkSearchResult> chunks =
      m_database->vecManager().getChunksByHash(m_database->db(), paperHash);
  if (chunks.isEmpty()) {
    qDebug() << "[Summarizer] no chunks for hash:" << paperHash;
    return QString();
  }

  QString context;
  const int overlapChars = 200; // matches PdfWorker::m_overlapChars

  for (int i = 0; i < chunks.size(); ++i) {
    const auto &c = chunks[i];
    QString text = c.text;

    // Strip the trailing overlap that will be repeated in the next chunk.
    // The last chunk keeps its full text — there's no successor to deduplicate.
    if (i < chunks.size() - 1 && text.length() > overlapChars) {
      text = text.left(text.length() - overlapChars);
    }

    context += QString("Page %1: %2\n\n").arg(c.page).arg(text);
  }

  // Count total OCR chars across all (pre-dedup) chunks
  int totalOcrChars = 0;
  for (const auto &c : chunks)
    totalOcrChars += c.text.length();

  qDebug() << "[Summarizer] retrieved" << chunks.size()
           << "chunks | OCR total:" << totalOcrChars
           << "chars (with overlap) | assembled context:" << context.length()
           << "chars (deduplicated) | first chunk sample:"
           << chunks.first().text.left(80) << "..."
           << "for hash:" << paperHash;
  return context;
}

std::vector<PageImage>
Summarizer::retrieveVisualContext(const QString &paperHash) const {
  std::vector<PageImage> result;

  QString imgPath = m_config->projectImgPath(m_config->lastProject());
  QDir imgDir(imgPath);
  if (!imgDir.exists())
    return result;

  imgDir.setNameFilters({paperHash + "_page_*.png"});
  QStringList paths;
  for (const QFileInfo &fi : imgDir.entryInfoList())
    paths.append(fi.absoluteFilePath());
  paths.sort();

  for (const QString &path : paths) {
    QImage img(path);
    if (img.isNull()) {
      qWarning() << "[Summarizer] Failed to load image:" << path;
      continue;
    }
    result.push_back(PageImage::fromQImage(img));
  }

  qDebug() << "[Summarizer] loaded" << result.size()
           << "page image(s) for hash:" << paperHash;
  return result;
}

/// Load only images from pages that contain figures or tables.
std::vector<PageImage>
Summarizer::retrieveReducedVisualContext(const QString &paperHash) const {
  std::vector<PageImage> result;

  // Find which pages have figures or tables from chunk metadata.
  QList<ChunkSearchResult> chunks =
      m_database->vecManager().getChunksByHash(m_database->db(), paperHash);

  QSet<int> pagesWithVisuals;
  for (const auto &c : chunks) {
    if (!c.pageMetadata.isEmpty()) {
      QJsonObject meta =
          QJsonDocument::fromJson(c.pageMetadata.toUtf8()).object();
      if (meta.value("has_figure").toBool(false) ||
          meta.value("has_vector_graphics").toBool(false) ||
          meta.value("has_table").toBool(false)) {
        pagesWithVisuals.insert(c.page);
      }
    }
  }

  if (pagesWithVisuals.isEmpty()) {
    qDebug() << "[Summarizer] reduced visual: no pages with figures/tables for"
             << paperHash;
    return result;
  }

  QString imgPath = m_config->projectImgPath(m_config->lastProject());
  QDir imgDir(imgPath);
  if (!imgDir.exists())
    return result;

  // Page numbers in the DB are 0-based; image files use 1-based (_page_001).
  for (int page0 : pagesWithVisuals) {
    QString fileName = QString("%1_page_%2.png")
                           .arg(paperHash)
                           .arg(page0 + 1, 3, 10, QChar('0'));
    QString fullPath = imgDir.absoluteFilePath(fileName);

    QImage img(fullPath);
    if (img.isNull()) {
      continue;
    }
    result.push_back(PageImage::fromQImage(img));
  }

  qDebug() << "[Summarizer] reduced visual:" << result.size()
           << "page image(s) for" << pagesWithVisuals.size()
           << "pages with figures/tables in" << paperHash;
  return result;
}

PromptPayload Summarizer::buildPrompt(const QString &paperHash) const {
  PromptPayload payload;
  payload.userMessage = s_userPrompt;

  if (!m_database || !m_database->isOpen()) {
    qWarning() << "[Summarizer] database not open";
    return payload;
  }

  QList<ChunkSearchResult> chunks =
      m_database->vecManager().getChunksByHash(m_database->db(), paperHash);
  if (chunks.isEmpty()) {
    qDebug() << "[Summarizer] no chunks for hash:" << paperHash;
    return payload;
  }

  VisualMode mode = m_config->visualModeEnum();

  // ── Merge chunks per page (0-based page → text) ──
  QMap<int, QString> pageTextMap;
  const int overlapChars = 200; // matches PdfWorker::m_overlapChars

  for (int i = 0; i < chunks.size(); ++i) {
    const auto &c = chunks[i];
    QString text = c.text;
    if (i < chunks.size() - 1 && text.length() > overlapChars)
      text = text.left(text.length() - overlapChars);
    pageTextMap[c.page] += text;
  }

  // ── Which pages get an image? ──
  QSet<int> pagesWithImage;
  if (mode == VisualMode::Full) {
    for (int p : pageTextMap.keys())
      pagesWithImage.insert(p);
  } else if (mode == VisualMode::Reduced) {
    for (const auto &c : chunks) {
      if (!c.pageMetadata.isEmpty()) {
        QJsonObject meta =
            QJsonDocument::fromJson(c.pageMetadata.toUtf8()).object();
        if (meta.value("has_figure").toBool(false) ||
            meta.value("has_vector_graphics").toBool(false) ||
            meta.value("has_table").toBool(false))
          pagesWithImage.insert(c.page);
      }
    }
  }

  // ── Load page images from disk ──
  QMap<int, QImage> pageImages;
  if (!pagesWithImage.isEmpty()) {
    QString imgPath = m_config->projectImgPath(m_config->lastProject());
    QDir imgDir(imgPath);
    if (imgDir.exists()) {
      for (int page0 : pagesWithImage) {
        QString fileName = QString("%1_page_%2.png")
                               .arg(paperHash)
                               .arg(page0 + 1, 3, 10, QChar('0'));
        QImage img(imgDir.absoluteFilePath(fileName));
        if (!img.isNull())
          pageImages[page0] = img.convertToFormat(QImage::Format_RGB888);
      }
    }
  }

  // ── Build ordered PageBlocks (1-based) ──
  auto sortedPages = pageTextMap.keys();
  std::sort(sortedPages.begin(), sortedPages.end());

  for (int page0 : sortedPages) {
    PageBlock pb;
    pb.pageNumber = page0 + 1;
    pb.text = pageTextMap[page0];
    auto it = pageImages.find(page0);
    if (it != pageImages.end())
      pb.visualContext = PageImage::fromQImage(it.value());
    payload.pageBlocks.push_back(std::move(pb));
  }

  // ── Legacy fields (kept for backward compat) ──
  payload.textContext = retrieveContext(paperHash);

  // ── System prompt selection ──
  switch (mode) {
  case VisualMode::TextOnly:
    payload.systemPrompt = s_systemPromptText;
    qDebug() << "[Summarizer] Visual: TEXT-ONLY mode —"
             << payload.pageBlocks.size() << "page(s) for" << paperHash;
    break;
  case VisualMode::Full: {
    payload.systemPrompt = s_systemPromptVisual;
    payload.visualContext = retrieveVisualContext(paperHash);
    int imgCount = 0;
    for (const auto &pb : payload.pageBlocks)
      if (!pb.visualContext.data.empty())
        imgCount++;
    qDebug() << "[Summarizer] Visual: FULL mode —" << payload.pageBlocks.size()
             << "page(s), " << imgCount << "image(s) for" << paperHash;
    break;
  }
  case VisualMode::Reduced: {
    payload.systemPrompt = s_systemPromptVisual;
    payload.visualContext = retrieveReducedVisualContext(paperHash);
    int imgCount = 0;
    for (const auto &pb : payload.pageBlocks)
      if (!pb.visualContext.data.empty())
        imgCount++;
    qDebug() << "[Summarizer] Visual: REDUCED mode —"
             << payload.pageBlocks.size() << "page(s), " << imgCount
             << "with image(s) for" << paperHash;
    break;
  }
  }

  return payload;
}

void Summarizer::stop() {
  m_stopping = true;
  // Signal cooperative cancellation to the in-flight local generation.
  // InferenceEngine::generate() polls this flag (and pumps the event loop
  // every 32 tokens so this slot can run) and stops at the next token
  // boundary. summarizePapers() sees m_cancelled, skips the DB write for the
  // current paper, and stops processing further papers.
  m_cancelled = true;
  // Cancel the in-flight remote request so its QEventLoop exits.
  m_remoteBackend->cancel();
  // Do NOT unload/reload models here:
  //  - the chat model may be held mid-llama_decode; unloading now would free
  //    it under a live inference (use-after-free).
  //  - the embedding model restore is already handled on the normal exit
  //    path by cleanupGpu(), which runs unconditionally at the end of
  //    summarizePapers() (whether all papers finished or we broke early).
  emit stopped();
  qDebug() << "[Summarizer] Stop requested";
}

/* ── Database persistence ───────────────────────────────────────── */

void Summarizer::saveSummary(const QString &hash, const PaperSummary &summary,
                             const QString &rawJson, const QString &modelName) {
  if (!m_database || !m_database->isOpen()) {
    qWarning() << "[Summarizer] Database not open — cannot save summary";
    return;
  }

  auto &db = m_database->db();

  SqliteStatement stmt =
      db.prepare("INSERT INTO Summaries "
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
    qCritical() << "[Summarizer] Failed to prepare summary upsert";
    return;
  }

  (void)stmt.bind(1, hash);
  (void)stmt.bind(2, summary.title);
  (void)stmt.bind(3, summary.authors.join("; "));
  (void)stmt.bind(4, summary.journal);
  (void)stmt.bind(5, QString::number(summary.year));
  (void)stmt.bind(6, summary.topics.join(", "));
  (void)stmt.bind(7, modelName);
  (void)stmt.bind(8, rawJson.toUtf8());
  (void)stmt.bind(9, QDateTime::currentDateTimeUtc());

  auto execResult = stmt.exec();
  if (!execResult) {
    qCritical() << "[Summarizer] Failed to save summary for" << hash << ":"
                << execResult.error();
    return;
  }

  if (m_database->summaryModel())
    m_database->summaryModel()->loadAll();

  // ── Update chunk metadata (clean_title + citation) ──
  QString citation =
      summary.authors.join(", ") + " (" + QString::number(summary.year) + ")";
  citation = citation.trimmed();

  // Escape single quotes for SQL literals.
  QString sqlTitle = summary.title;
  sqlTitle.replace("'", "''");
  QString sqlCitation = citation;
  sqlCitation.replace("'", "''");
  QString sqlHash = hash;
  sqlHash.replace("'", "''");

  auto updateResult =
      db.exec(QString("UPDATE chunks SET clean_title = '%1', citation = '%2' "
                      "WHERE paper_hash = '%3'")
                  .arg(sqlTitle, sqlCitation, sqlHash));
  if (!updateResult) {
    qWarning() << "[Summarizer] Failed to update chunks metadata for" << hash
               << ":" << updateResult.error();
  }

  // ── Register in Citations table (OpenAlex will fill cites/cited_by) ──
  SqliteStatement refStmt =
      db.prepare("INSERT INTO Citations (paper_hash, clean_title, work_id, "
                 "cites, cited_by) VALUES (?, ?, '', '', '') "
                 "ON CONFLICT(paper_hash) DO UPDATE SET clean_title = "
                 "excluded.clean_title");
  if (refStmt.isValid()) {
    (void)refStmt.bind(1, hash);
    (void)refStmt.bind(2, summary.title);
    (void)refStmt.exec();
  }

  qDebug() << "[Summarizer] Saved summary for" << hash
           << "— title:" << summary.title;
}
