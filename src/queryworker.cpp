#include "queryworker.h"
#include "configmanager.h"
#include "database.h"
#include "localbackend.h"
#include "remotebackend.h"
#include "sqlitevec.h"
#include "textformat.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QTimer>

/* ── Prompts ───────────────────────────────────────────────────── */

static const QString s_systemPrompt = []() {
  return R"(You are an expert academic research assistant.
You are provided with highly relevant pages extracted from multiple research papers.
Each page is presented with its image (when available) followed by its extracted text.
Synthesize a cohesive, detailed, comprehensive response answering the user's query utilizing both
the textual evidence and visual figures/tables/charts provided.

Citations: each page context is labelled with a reference number in square brackets, e.g. [3].
Cite every claim taken from the context inline using ONLY these reference numbers,
e.g. "... latent classes [3]." or "... as shown in [2][5]."
Never write author names, years or page numbers in a citation yourself, and ignore any page
numbers printed inside the page text: the reference numbers are converted into full citations
automatically. Do NOT add a references, bibliography or sources section at the end.

Mathematics: NEVER use LaTeX ($...$, $$...$$, \(...\), \frac, \text, \epsilon, _{...}, ^{...}).
Write formulas as plain Unicode text: Greek letters (α β γ ε θ μ σ π),
superscripts/subscripts (x², xᵢ, Uᵢₙ), and symbols (√ ± × ÷ ≠ ≤ ≥ ≈ ∑ ∫ → ∞ Δ ∈ ∀).
Prefer a flat, readable form (e.g. "Uᵢₙ = V(Xₙ, Xᵢ; b) + εᵢₙ", "μ = 5.2, σ = 1.3");
write fractions as "a/b".
Output the response in Markdown.)";
}();

/// Formatting reminder appended at the very end of the user message: models
/// weigh the end of the prompt most, and the system prompt alone is often
/// ignored after a long context.
static const QString s_formatReminder = QStringLiteral(
    "Formatting rules (mandatory): cite only with the bracketed reference "
    "numbers of the page contexts, e.g. [3] or [2][5] — no author names or "
    "page numbers; no final references section; no LaTeX at all — write math "
    "in plain Unicode (εᵢₙ, x², ≠, ≤).");

/// Pages are stored 0-based in the database; readers and the page image
/// files (hash_page_001.png) use 1-based numbering.
static int displayPage(const PageContext &pc) { return pc.page + 1; }

/// Short inline label for a paper: its formatted citation, else its title,
/// else a hash prefix.
static QString inlineLabel(const PageContext &pc) {
  if (!pc.citation.isEmpty())
    return pc.citation;
  if (!pc.cleanTitle.isEmpty())
    return pc.cleanTitle;
  return pc.paperHash.left(12) + "...";
}

/// Replace the model's reference tags ([3], [2][5], [2, 5]) with full inline
/// citations built from the retrieved pages, e.g.
/// "(Walker, Ben-Akiva (2002), pp. 5, 7; Marley (2002), p. 2)", so the inline
/// pages always match the Sources footer. Tags that reference no known page
/// context are left untouched; Markdown links "[x](url)" are skipped.
static QString resolveCitations(const QString &text,
                                const QList<PageContext> &pages) {
  static const QRegularExpression tags(QStringLiteral(
      "(?:\\[\\s*\\d+(?:\\s*[,;]\\s*\\d+)*\\s*\\])+(?!\\()"));
  static const QRegularExpression number(QStringLiteral("\\d+"));

  QString out;
  qsizetype last = 0;
  auto it = tags.globalMatch(text);
  while (it.hasNext()) {
    const auto mt = it.next();
    out += text.mid(last, mt.capturedStart() - last);
    last = mt.capturedEnd();

    // Group the referenced pages by paper, in citation order.
    QStringList order;
    QHash<QString, QList<int>> pagesByLabel;
    auto nums = number.globalMatch(mt.captured(0));
    while (nums.hasNext()) {
      const int idx = nums.next().captured(0).toInt() - 1;
      if (idx < 0 || idx >= pages.size())
        continue;
      const QString label = inlineLabel(pages[idx]);
      if (!pagesByLabel.contains(label))
        order.append(label);
      QList<int> &list = pagesByLabel[label];
      if (!list.contains(displayPage(pages[idx])))
        list.append(displayPage(pages[idx]));
    }
    if (order.isEmpty()) {
      out += mt.captured(0);
      continue;
    }

    QStringList parts;
    for (const QString &label : order) {
      QList<int> list = pagesByLabel.value(label);
      std::sort(list.begin(), list.end());
      QStringList pageNums;
      for (int p : list)
        pageNums.append(QString::number(p));
      parts.append(TextFormat::escapeMarkdown(label) +
                   (list.size() > 1 ? ", pp. " : ", p. ") +
                   pageNums.join(", "));
    }
    out += "(" + parts.join("; ") + ")";
  }
  out += text.mid(last);
  return out;
}

/// Label identifying a source: "citation: title", the title alone, or a hash
/// prefix when the paper has no metadata.
static QString sourceLabel(const PageContext &pc) {
  if (pc.cleanTitle.isEmpty())
    return pc.paperHash.left(12) + "...";
  return pc.citation.isEmpty() ? pc.cleanTitle
                               : pc.citation + ": " + pc.cleanTitle;
}

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
  // A stop() from a previous, abandoned run may have set the cancel flag;
  // clear it so this fresh query is not cancelled immediately.
  m_cancelled = false;
  if (queryText.trimmed().isEmpty()) {
    emit queryFinished(QString(), false, tr("Empty query"));
    return;
  }

  // Mark a DB operation as in flight so DatabaseManager::switchDatabase()
  // refuses to close the shared connection while we are using it.
  if (m_database)
    m_database->beginDbOperation();

  qDebug() << "[QueryWorker] Starting query:" << queryText.left(60)
           << "| maxPages:" << maxPages;

  QElapsedTimer totalTimer;
  totalTimer.start();

  // 1. Retrieve & group pages from vector search
  QList<PageContext> pages = retrieveAndGroupPages(queryText, maxPages);
  if (pages.isEmpty()) {
    emit queryFinished(QString(), false,
                       tr("No relevant information found in the library."));
    if (m_database)
      m_database->endDbOperation();
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
  std::expected<QString, QString> inference =
      backend.isLocal ? queryLocal(payload, backend)
                      : queryRemote(payload, backend);

  if (m_cancelled) {
    // The user stopped the query mid-inference. Do NOT persist a partial
    // response — the run was abandoned, not completed.
    emit queryFinished(QString(), false, tr("Query cancelled"));
    qDebug() << "[QueryWorker] ✗ Query cancelled by user — no DB write";
  } else if (!inference) {
    emit queryFinished(QString(), false, inference.error());
    qDebug() << "[QueryWorker] ✗ Query failed:" << inference.error();
  } else if (inference->trimmed().isEmpty()) {
    QString msg = tr("Empty response from model");
    emit queryFinished(QString(), false, msg);
    qDebug() << "[QueryWorker] ✗ Query failed:" << msg;
  } else {
    // Models often emit LaTeX despite the prompt; MarkdownText can't render it.
    QString result = TextFormat::latexToUnicode(*inference,
                                                TextFormat::Target::Markdown);
    // Turn [n] reference tags into citations matching the Sources footer.
    result = resolveCitations(result, pages);
    // Append sources footer. The label is deliberately not translated: the
    // response is persisted, and "Sources" reads the same in English and
    // French, so stored history stays consistent across UI languages.
    if (!citations.isEmpty())
      result += "\n\n**Sources:**\n" + citations.join("\n");
    saveQuery(queryText, result, backend.modelName);
    emit queryFinished(result, true, QString());
    qDebug() << "[QueryWorker] ✓ Query answered (" << result.length()
             << "chars) in" << totalTimer.elapsed() << "ms";
  }

  cleanupGpu(backend.isLocal, wasEmbeddingReady);
  if (m_database)
    m_database->endDbOperation();
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

std::expected<QString, QString>
QueryWorker::queryLocal(const PromptPayload &payload,
                        const BackendInfo &backend) {
  if (backend.modelPath.isEmpty()) {
    qWarning() << "[QueryWorker] No .gguf model found for" << backend.modelName;
    return std::unexpected(
        tr("No model file found for \"%1\"").arg(backend.modelName));
  }
  // The backend counts the exact prompt tokens during its no-alloc probe and
  // sizes the KV cache as prompt_tokens + headroom, so no char heuristic here.
  qDebug() << "[QueryWorker] Loading local model:" << backend.modelPath;
  QElapsedTimer loadTimer;
  loadTimer.start();

  auto loadResult =
      m_chatBackend->loadModel(backend.modelPath, payload, m_headroom);
  if (!loadResult) {
    qWarning() << "[QueryWorker] Failed to load model:" << loadResult.error();
    return std::unexpected(
        tr("Failed to load model: %1").arg(loadResult.error()));
  }
  qDebug() << "[QueryWorker] Model loaded in" << loadTimer.elapsed() << "ms";

  QElapsedTimer genTimer;
  genTimer.start();
  // Pass the cancel flag so a queued stop() can interrupt a long local
  // generation at the next token boundary (see InferenceEngine::generate).
  QString result = m_chatBackend->summarize(payload, &m_cancelled);
  qDebug() << "[QueryWorker] Generation completed in" << genTimer.elapsed()
           << "ms"
           << "(" << result.length() << "chars)"
           << (m_cancelled ? "— CANCELLED" : "");
  return result;
}

/* ── Remote query ───────────────────────────────────────────────── */

std::expected<QString, QString>
QueryWorker::queryRemote(const PromptPayload &payload,
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
  bool timedOut = false;
  QEventLoop loop;
  QObject::connect(
      m_remoteBackend, &RemoteBackend::finished, this,
      [&result, &error, &loop](const QString &text, const QString &err) {
        result = text;
        error = err;
        loop.quit();
      },
      Qt::SingleShotConnection);

  // Watchdog: if the endpoint hangs (no response, server stall) the reply
  // never finishes and this loop would block the worker thread forever,
  // wedging the controller. Abort after the budget and surface an error.
  // Generous — a large multimodal prompt can take minutes to process.
  const int kRemoteTimeoutMs = 10 * 60 * 1000; // 10 minutes
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, this, [&loop, &timedOut]() {
    timedOut = true;
    if (loop.isRunning())
      loop.quit();
  });
  timeout.start(kRemoteTimeoutMs);

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
  timeout.stop();

  qDebug() << "[QueryWorker] Remote generation completed in"
           << genTimer.elapsed() << "ms"
           << "(" << result.length() << "chars)"
           << (timedOut ? "— TIMED OUT" : "");

  if (timedOut) {
    // The reply never finished; cancel the still-in-flight request so it
    // doesn't fire later into a dead loop, and report the failure.
    m_remoteBackend->cancel();
    qWarning() << "[QueryWorker] Remote request timed out after"
               << kRemoteTimeoutMs / 1000 << "s";
    return std::unexpected(tr("Remote request timed out after %1 minutes")
                               .arg(kRemoteTimeoutMs / 60000));
  }
  if (!error.isEmpty()) {
    qWarning() << "[QueryWorker] Remote request failed:" << error;
    return std::unexpected(tr("Remote request failed: %1").arg(error));
  }
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
                            .arg(displayPage(pc), 3, 10, QChar('0'));
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

  // Keep sources in retrieval (relevance) order, not alphabetical order.
  QStringList sourceOrder;
  for (const auto &pc : pages) {
    QString src = sourceLabel(pc);
    if (!sourcePages.contains(src))
      sourceOrder.append(src);
    sourcePages[src].insert(displayPage(pc));
  }

  QStringList citations;
  for (const QString &src : sourceOrder) {
    QList<int> sortedPages = sourcePages.value(src).values();
    std::sort(sortedPages.begin(), sortedPages.end());
    QString pagesStr = QString();
    for (int i = 0; i < sortedPages.size(); ++i) {
      if (i > 0)
        pagesStr += ", ";
      pagesStr += QString::number(sortedPages[i]);
    }
    citations.append("- " + TextFormat::escapeMarkdown(src) +
                     " (Pages: " + pagesStr +
                     ")");
  }

  return citations;
}

/* ── Prompt building ────────────────────────────────────────────── */

PromptPayload QueryWorker::buildPrompt(const QList<PageContext> &pages,
                                       const QString &queryText) const {
  PromptPayload payload;
  payload.systemPrompt = s_systemPrompt;

  const bool useVisual = m_config->useVisualContext();

  // Build structured text context: one block per page.
  QStringList blocks;
  for (int i = 0; i < pages.size(); ++i) {
    const PageContext &pc = pages[i];
    const QString src = sourceLabel(pc);

    // The bracketed number is the reference tag the model cites with; it is
    // resolved back to this page by resolveCitations().
    QString block = QString("=== PAGE CONTEXT [%1] — %2, page %3 ===\n")
                        .arg(i + 1)
                        .arg(src)
                        .arg(displayPage(pc));

    for (const QString &chunk : pc.textChunks)
      block += chunk + "\n[---]\n";

    block += "\n";
    blocks.append(block);

    // Build PageBlock with image
    PageBlock pb;
    pb.pageNumber = i + 1;
    pb.text = block;

    if (useVisual && !pc.imageFile.isEmpty()) {
      QImage img(pc.imageFile);
      if (!img.isNull()) {
        pb.visualContext = PageImage::fromQImage(img);
        // Legacy (non-interleaved) multimodal path: reuse the decoded pixels
        // instead of decoding the PNG a second time.
        payload.visualContext.push_back(pb.visualContext);
      }
    }
    payload.pageBlocks.push_back(std::move(pb));
  }

  payload.textContext = blocks.join("\n");
  payload.userMessage = "Core Task Query: " + queryText +
                        "\n\n" + s_formatReminder +
                        "\n\nSynthesize your final cross-paper response now:";

  if (useVisual)
    qDebug() << "[QueryWorker] Visual context —" << payload.visualContext.size()
             << "image(s) loaded";

  return payload;
}

const QString &QueryWorker::systemPrompt() { return s_systemPrompt; }

void QueryWorker::stop() {
  m_stopping = true;
  // Signal cooperative cancellation to the in-flight local generation.
  // InferenceEngine::generate() polls this flag (and pumps the event loop
  // every 32 tokens so this slot can run) and stops at the next token
  // boundary. executeQuery() sees m_cancelled and skips the DB write.
  m_cancelled = true;
  // Cancel the in-flight remote request so its QEventLoop exits.
  m_remoteBackend->cancel();
  // Do NOT unload the chat model here: it may be held mid-llama_decode.
  // cleanupGpu() unloads it exactly once on the normal exit path.
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
