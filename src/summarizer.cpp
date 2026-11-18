#include "summarizer.h"
#include "configmanager.h"
#include "database.h"
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
For mathematics, do NOT use LaTeX or markdown. Write formulas as plain
Unicode text so they render directly: use Greek letters (α β γ θ μ σ π),
superscripts/subscripts (x², xᵢ, aⱼ), and symbols (√, ², ³, ±, ×, ÷, ≤, ≥,
≈, ∑, ∫, →, ∞, Δ). Prefer a flat, readable form (e.g. "x² + y² = r²",
"μ = 5.2, σ = 1.3") over stacked fractions — write fractions as "a/b".
Don't use markdown formatting. Return only valid JSON.)";
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
For mathematics, do NOT use LaTeX or markdown. Write formulas as plain
Unicode text so they render directly: use Greek letters (α β γ θ μ σ π),
superscripts/subscripts (x², xᵢ, aⱼ), and symbols (√, ², ³, ±, ×, ÷, ≤, ≥,
≈, ∑, ∫, →, ∞, Δ). Prefer a flat, readable form (e.g. "x² + y² = r²",
"μ = 5.2, σ = 1.3") over stacked fractions — write fractions as "a/b".
Don't use markdown formatting. Return only valid JSON.)";
}();

static const QString s_userPrompt = []() {
  return R"(Summarize this paper:)";
}();

const QString &Summarizer::systemPrompt() { return s_systemPromptVisual; }
const QString &Summarizer::userPrompt() { return s_userPrompt; }

/* ── Construction / destruction ─────────────────────────────────── */

Summarizer::Summarizer(ConfigManager *config, DatabaseManager *database,
                       QObject *parent)
    : QObject(parent), m_config(config), m_database(database) {}

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

  const ActiveBackend backend = resolveActiveBackend(m_config);
  m_backend = makeChatBackend(backend);
  qDebug() << "[Summarizer] Backend:" << backendKindName(backend.kind)
           << "| Model:" << backend.modelName;

  const bool wasEmbeddingReady = prepareGpu();

  int successCount = 0;
  for (int i = 0; i < hashes.size(); ++i) {
    // Process pending events so that a queued stop() is handled.
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 100);
    if (isStopping())
      break;

    const QString &hash = hashes[i];
    qDebug() << "[Summarizer] Processing paper" << i + 1 << "/" << hashes.size()
             << "—" << hash;
    const QString title = paperTitle(hash);

    if (!m_database || !m_database->isOpen()) {
      emit paperFinished(hash, title, false,
                         tr("The project database is not open"));
      qWarning() << "[Summarizer] ✗ Paper" << hash << ": database not open";
      continue;
    }

    PromptPayload payload = buildPrompt(hash);

    std::expected<QString, QString> result = generate(payload, backend);

    if (m_cancelled) {
      // stop() fired mid-inference. Do NOT persist a partial summary and stop
      // processing further papers.
      emit paperFinished(hash, title, false, tr("Cancelled"));
      qDebug() << "[Summarizer] ✗ Paper" << hash << "cancelled — no DB write";
      break;
    }

    if (!result) {
      emit paperFinished(hash, title, false, result.error());
      qWarning() << "[Summarizer] ✗ Paper" << hash
                 << "failed:" << result.error();
    } else if (result->trimmed().isEmpty()) {
      QString msg = tr("The model returned an empty response");
      emit paperFinished(hash, title, false, msg);
      qWarning() << "[Summarizer] ✗ Paper" << hash << "failed:" << msg;
    } else if (const auto parsed = SummaryFormat::tryParseJson(*result);
               !parsed) {
      // Unreadable: saving it would store a summary with no title or
      // authors (and no OpenAlex match).  Fail so the paper can be redone.
      QString msg = tr("The model's response is not a readable summary");
      emit paperFinished(hash, title, false, msg);
      qWarning() << "[Summarizer] ✗ Paper" << hash << "failed:" << msg
                 << "| response:" << result->left(200);
    } else {
      const PaperSummary &summary = *parsed;
      auto saved = saveSummary(hash, summary, *result, backend.modelName);
      if (!saved) {
        emit paperFinished(hash, title, false,
                           tr("The summary could not be saved: %1")
                               .arg(saved.error()));
        continue;
      }

      successCount++;
      emit paperFinished(
          hash, summary.title.isEmpty() ? title : summary.title, true,
          tr("Summary generated and saved (%1 chars)").arg(result->length()));
      qDebug() << "[Summarizer] ✓ Paper" << hash << "summarized and saved"
               << "(" << result->length() << "chars)";
    }
  }

  emit allFinished(successCount, hashes.size());
  qDebug() << "[Summarizer] All done —" << successCount << "/" << hashes.size()
           << "succeeded in" << totalTimer.elapsed() << "ms";

  cleanupGpu(wasEmbeddingReady);
  if (m_database)
    m_database->endDbOperation();
}

/* ── GPU preparation ────────────────────────────────────────────── */

bool Summarizer::prepareGpu() {
  if (!m_backend || !m_backend->caps().usesLocalGpu || !m_database)
    return false;

  SqliteVecManager &vecMgr = m_database->vecManager();
  if (vecMgr.isReady()) {
    vecMgr.shutdown();
    qDebug() << "[Summarizer] Embedding model unloaded to free GPU memory";
    return true;
  }
  return false;
}

void Summarizer::cleanupGpu(bool wasEmbeddingReady) {
  if (wasEmbeddingReady && m_database) {
    qDebug() << "[Summarizer] Reloading embedding model...";
    QElapsedTimer t;
    t.start();
    m_database->vecManager().initialize();
    qDebug() << "[Summarizer] Embedding model reloaded in" << t.elapsed()
             << "ms";
  }
  if (m_backend)
    m_backend->release();
  m_backend.reset();
}

/* ── Inference ──────────────────────────────────────────────────── */

std::expected<QString, QString>
Summarizer::generate(const PromptPayload &payload,
                     const ActiveBackend &active) {
  GenerationOptions options;
  options.responseHeadroom = m_headroom;
  options.forceJson = true; // remote: the summary is a JSON document
  auto result = m_backend->generate(payload, options, &m_cancelled);
  if (result)
    return *result;

  const GenerationError &e = result.error();
  switch (e.kind) {
  case GenerationError::Kind::NoModelFile:
    return std::unexpected(
        tr("No model file found for \"%1\". Check the selected model.")
            .arg(active.modelName));
  case GenerationError::Kind::LoadFailed:
    return std::unexpected(
        tr("The model could not be loaded: %1").arg(e.detail));
  case GenerationError::Kind::Timeout: // no timeout requested
  case GenerationError::Kind::RequestFailed:
    break;
  }
  return std::unexpected(tr("Remote request failed: %1").arg(e.detail));
}

/* ── Prompt building ────────────────────────────────────────────── */

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
          pageImages[page0] = img;
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
      pb.visualContext = PageImage::forModel(it.value());
    payload.pageBlocks.push_back(std::move(pb));
  }

  // ── System prompt selection ──
  // The visual prompt only when images actually made it into the payload
  // (Reduced mode on a paper without figures has none).  Backends that
  // cannot pass the images fall back to textOnlySystemPrompt.
  int imgCount = 0;
  for (const auto &pb : payload.pageBlocks)
    if (!pb.visualContext.data.empty())
      ++imgCount;
  const bool visual = mode != VisualMode::TextOnly && imgCount > 0;
  payload.systemPrompt = visual ? s_systemPromptVisual : s_systemPromptText;
  if (visual)
    payload.textOnlySystemPrompt = s_systemPromptText;

  const char *modeName = mode == VisualMode::Full      ? "FULL"
                         : mode == VisualMode::Reduced ? "REDUCED"
                                                       : "TEXT-ONLY";
  qDebug() << "[Summarizer] Visual:" << modeName << "mode —"
           << payload.pageBlocks.size() << "page(s)," << imgCount
           << "image(s) for" << paperHash;

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
  if (m_backend)
    m_backend->cancel();
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

std::expected<void, QString>
Summarizer::saveSummary(const QString &hash, const PaperSummary &summary,
                        const QString &rawJson, const QString &modelName) {
  if (!m_database || !m_database->isOpen()) {
    qWarning() << "[Summarizer] Database not open — cannot save summary";
    return std::unexpected(tr("the project database is not open"));
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
    return std::unexpected(tr("database error (prepare failed)"));
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
    return std::unexpected(execResult.error());
  }

  // Do NOT reload SummaryModel here: this runs on the Summarizer thread and the
  // model is owned by / bound to the GUI thread.  The UI reloads it on the
  // main thread from paperFinished / allFinished (SummaryDbPane.qml).

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
                 "excluded.clean_title, "
                 // Re-summarised: the metadata may have improved, so give an
                 // unmatched paper another OpenAlex lookup right away.
                 "lookup_status = CASE WHEN work_id = '' THEN 'pending' "
                 "ELSE lookup_status END");
  if (refStmt.isValid()) {
    (void)refStmt.bind(1, hash);
    (void)refStmt.bind(2, summary.title);
    (void)refStmt.exec();
  }

  qDebug() << "[Summarizer] Saved summary for" << hash
           << "— title:" << summary.title;
  return {};
}

QString Summarizer::paperTitle(const QString &hash) const {
  if (m_database && m_database->isOpen()) {
    SqliteStatement stmt = m_database->db().prepare(
        "SELECT title FROM Summaries WHERE paper_hash = ?");
    if (stmt.isValid()) {
      (void)stmt.bind(1, hash);
      auto step = stmt.step();
      if (step && *step) {
        const QString title = stmt.columnText(0).trimmed();
        if (!title.isEmpty())
          return title;
      }
    }
  }
  return hash;
}
