#include "queryworker.h"
#include "configmanager.h"
#include "database.h"
#include "sqlitevec.h"
#include "textformat.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QRegularExpression>
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

Layout: output the response in Markdown, structured for easy reading.
- Start with a short introduction (2-3 sentences).
- Organize the body into sections with "### " headings.
- Keep paragraphs short: 2-4 sentences each, one idea per paragraph,
  separated by a blank line. Never write a paragraph longer than 5 sentences.
- Use bullet lists ("- **Term:** explanation") for enumerations of factors,
  findings or results, with a blank line before the list.
- End with a short "### Conclusion" section.)";
}();

/// Formatting reminder appended at the very end of the user message: models
/// weigh the end of the prompt most, and the system prompt alone is often
/// ignored after a long context.
static const QString s_formatReminder = QStringLiteral(
    "Formatting rules (mandatory): cite only with the bracketed reference "
    "numbers of the page contexts, e.g. [3] or [2][5] — no author names or "
    "page numbers; no final references section; no LaTeX at all — write math "
    "in plain Unicode (εᵢₙ, x², ≠, ≤); \"### \" section headings; short "
    "paragraphs of 2-4 sentences separated by blank lines; bullet lists for "
    "enumerations.");

/// Pages are stored 0-based in the database; readers and the page image
/// files (hash_page_001.png) use 1-based numbering.
/* ── Retrieval tuning ───────────────────────────────────────────── */

// Chunks farther than (best distance + margin) from the query are dropped,
// even if the budget has room: they would only add noise.  L2 distance on
// L2-normalised E5 embeddings (0 = identical, √2 ≈ 1.41 = orthogonal).
// E5 similarities are compressed into a narrow band, hence a margin relative
// to the best hit rather than an absolute threshold.
static constexpr float RelevanceMargin = 0.15f;
// Visual mode: tokens charged per page image (matches the local backend's
// per-image estimate) and the most images sent — encoding is slow.
static constexpr int ImageTokens = 800;
static constexpr int MaxImages = 12;
// Prompt tokens per page block header ("=== PAGE CONTEXT [n] — …") and per
// chunk separator, so the budget isn't overrun by the scaffolding.
static constexpr int PageHeaderTokens = 32;
static constexpr int ChunkSeparatorTokens = 4;

static int displayPage(const PageContext &pc) { return pc.page + 1; }

/// Short author-year label and full title of a paper, for citations.
struct PaperLabel {
  QString authors; ///< e.g. "Walker & Ben-Akiva", or a fallback label
  QString year;    ///< empty when unknown
  QString title;   ///< full title, empty when unknown

  /// "Walker & Ben-Akiva, 2002" — inside an inline citation.
  QString inlineForm() const {
    return year.isEmpty() ? authors : authors + ", " + year;
  }
  /// "Walker & Ben-Akiva (2002)" — in the Sources footer.
  QString footerForm() const {
    return year.isEmpty() ? authors : authors + " (" + year + ")";
  }
};

/// Compact an author list "A. Walker, M. Ben-Akiva, C. Marley" into
/// "Walker et al." (or "Walker & Ben-Akiva" for two authors).
static QString compactAuthors(const QString &authors) {
  QStringList names;
  for (const QString &a : authors.split(QRegularExpression("\\s*[,;]\\s*"),
                                        Qt::SkipEmptyParts)) {
    const QStringList words = a.simplified().split(' ');
    names.append(words.last());
  }
  if (names.isEmpty())
    return authors.trimmed();
  if (names.size() == 1)
    return names.first();
  if (names.size() == 2)
    return names[0] + " & " + names[1];
  return names.first() + " et al.";
}

static QString elide(const QString &s, int max) {
  return s.size() <= max ? s : s.left(max - 1).trimmed() + QStringLiteral("…");
}

/// Label of a paper.  Summarized papers carry "Authors (year)" in `citation`;
/// unsummarized ones only have their file name as title and citation, often
/// in the Zotero form "Authors - Year - Title".
static PaperLabel paperLabel(const PageContext &pc) {
  static const QRegularExpression authorYear(
      QStringLiteral("^(.+?)\\s*\\((\\d{4}|0)\\)$"));
  static const QRegularExpression zotero(
      QStringLiteral("^(.+?)\\s+-\\s+(\\d{4})\\s+-\\s+(.+)$"));
  static const QRegularExpression zoteroNoYear(
      QStringLiteral("^(.{2,60}?)\\s+-\\s+(.+)$"));

  const QString title = pc.cleanTitle.simplified();
  const QString citation = pc.citation.simplified();

  if (!citation.isEmpty() && citation != title) {
    const auto m = authorYear.match(citation);
    if (m.hasMatch()) {
      const QString year = m.captured(2) == "0" ? QString() : m.captured(2);
      return {compactAuthors(m.captured(1)), year, title};
    }
    return {elide(citation, 60), QString(), title};
  }
  if (title.isEmpty())
    return {pc.paperHash.left(12) + "…", QString(), QString()};
  if (const auto m = zotero.match(title); m.hasMatch())
    return {m.captured(1), m.captured(2), m.captured(3)};
  if (const auto m = zoteroNoYear.match(title); m.hasMatch())
    return {m.captured(1), QString(), m.captured(2)};
  // A bare file name such as "Zanardelli et al": short enough to stand alone.
  return {elide(title, 50), QString(), title.size() > 50 ? title : QString()};
}

static QString pageRange(QList<int> list) {
  std::sort(list.begin(), list.end());
  list.erase(std::unique(list.begin(), list.end()), list.end());
  QStringList nums;
  for (int p : list)
    nums.append(QString::number(p));
  return (list.size() > 1 ? "pp. " : "p. ") + nums.join(", ");
}

/// Replace the model's reference tags ([3], [2][5], [2, 5]) with compact
/// author-year citations built from the retrieved pages, e.g.
/// "(Walker & Ben-Akiva, 2002, pp. 5, 7; Marley, 2002, p. 2)", so the
/// inline pages always match the Sources footer.  The indices of the cited
/// page contexts are added to `cited`.  Tags that reference no known page
/// context are left untouched; Markdown links "[x](url)" are skipped.
static QString resolveCitations(const QString &text,
                                const QList<PageContext> &pages,
                                QSet<int> &cited) {
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
      cited.insert(idx);
      const QString label = paperLabel(pages[idx]).inlineForm();
      if (!pagesByLabel.contains(label))
        order.append(label);
      pagesByLabel[label].append(displayPage(pages[idx]));
    }
    if (order.isEmpty()) {
      out += mt.captured(0);
      continue;
    }

    QStringList parts;
    for (const QString &label : order)
      parts.append(TextFormat::escapeMarkdown(label) + ", " +
                   pageRange(pagesByLabel.value(label)));
    // Keep a space before the citation even if the model glued the tag to
    // the previous word ("absence[3]").
    if (!out.isEmpty() && !out.back().isSpace() && out.back() != '(')
      out += ' ';
    out += "(" + parts.join("; ") + ")";
  }
  out += text.mid(last);
  return out;
}

/// Label identifying a source in the prompt: "label — title" or the label.
static QString sourceLabel(const PageContext &pc) {
  const PaperLabel l = paperLabel(pc);
  return l.title.isEmpty() ? l.footerForm()
                           : l.footerForm() + " — " + l.title;
}

/* ── Construction / destruction ─────────────────────────────────── */

QueryWorker::QueryWorker(ConfigManager *config, DatabaseManager *database,
                         QObject *parent)
    : QObject(parent), m_config(config), m_database(database) {}

QueryWorker::~QueryWorker() = default;

/* ── Public API ─────────────────────────────────────────────────── */

void QueryWorker::executeQuery(const QString &queryText, int contextTokens) {
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

  QElapsedTimer totalTimer;
  totalTimer.start();

  // 1. Resolve the backend: it decides the default prompt budget.
  const ActiveBackend backend = resolveActiveBackend(m_config);
  qDebug() << "[QueryWorker] Backend:" << backendKindName(backend.kind)
           << "| Model:" << backend.modelName;

  if (contextTokens <= 0)
    contextTokens =
        estimateContextBudget(backend, 0, ResponseHeadroom).autoTokens;
  // The page context gets what the system prompt, the instructions and the
  // question leave of the budget.
  const int fixedTokens =
      countTokens(s_systemPrompt + s_formatReminder + queryText) + 64;
  const int budget = qMax(1024, contextTokens - fixedTokens);
  qDebug() << "[QueryWorker] Starting query:" << queryText.left(60)
           << "| context:" << contextTokens << "tokens | page budget:"
           << budget;

  // 2. Retrieve & group pages from vector search
  QList<PageContext> pages = retrieveAndGroupPages(queryText, budget);
  if (pages.isEmpty()) {
    // Tell a real failure (no database, embedding model unusable) from a
    // library that simply has nothing relevant.
    QString why = tr("No relevant information found in the library.");
    if (!m_database || !m_database->isOpen())
      why = tr("The project database is not open.");
    else if (!m_database->vecManager().isReady() &&
             !m_database->vecManager().lastError().isEmpty())
      why = tr("The library cannot be searched: %1")
                .arg(m_database->vecManager().lastError());
    emit queryFinished(QString(), false, why);
    if (m_database)
      m_database->endDbOperation();
    return;
  }

  // 4. Build prompt (the embedding model stays loaded: vector search is
  // done, and a query is a single generation).
  PromptPayload payload = buildPrompt(pages, queryText);
  qDebug() << "[QueryWorker] Prompt built — text context:"
           << payload.textContext.length()
           << "chars, visuals:" << payload.visualContext.size();

  // 5. Run inference
  m_backend = makeChatBackend(backend);
  std::expected<QString, QString> inference = generate(payload, backend);

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
    // Bold-only title lines followed by a single newline would otherwise be
    // glued to their paragraph.
    result = TextFormat::normalizeMarkdown(result);
    // Turn [n] reference tags into citations matching the Sources footer.
    QSet<int> cited;
    result = resolveCitations(result, pages, cited);
    const QStringList citations = buildCitationsFooter(pages, cited);
    // Append sources footer. The label is deliberately not translated: the
    // response is persisted, and "Sources" reads the same in English and
    // French, so stored history stays consistent across UI languages.
    if (!citations.isEmpty())
      result += "\n\n---\n\n**Sources**\n\n" + citations.join("\n");
    // On success the error argument carries a warning when the answer could
    // not be stored in the query history.
    auto saved = saveQuery(queryText, result, backend.modelName);
    emit queryFinished(result, true,
                       saved ? QString()
                             : tr("The answer could not be saved in the "
                                  "history: %1")
                                   .arg(saved.error()));
    qDebug() << "[QueryWorker] ✓ Query answered (" << result.length()
             << "chars) in" << totalTimer.elapsed() << "ms";
  }

  releaseBackend();
  if (m_database)
    m_database->endDbOperation();
}

/* ── Backend lifecycle ──────────────────────────────────────────── */

void QueryWorker::releaseBackend() {
  if (m_backend)
    m_backend->release();
  m_backend.reset();
}

/* ── Inference ──────────────────────────────────────────────────── */

std::expected<QString, QString>
QueryWorker::generate(const PromptPayload &payload,
                      const ActiveBackend &active) {
  GenerationOptions options;
  options.responseHeadroom = ResponseHeadroom;
  options.timeoutMs = RemoteTimeoutMs;
  auto result = m_backend->generate(payload, options, &m_cancelled);
  if (result)
    return *result;

  const GenerationError &e = result.error();
  switch (e.kind) {
  case GenerationError::Kind::NoModelFile:
    qWarning() << "[QueryWorker] No .gguf model found for" << active.modelName;
    return std::unexpected(
        tr("No model file found for \"%1\"").arg(active.modelName));
  case GenerationError::Kind::LoadFailed:
    return std::unexpected(tr("Failed to load model: %1").arg(e.detail));
  case GenerationError::Kind::Timeout:
    return std::unexpected(tr("Remote request timed out after %1 minutes")
                               .arg(RemoteTimeoutMs / 60000));
  case GenerationError::Kind::RequestFailed:
    break;
  }
  return std::unexpected(tr("Remote request failed: %1").arg(e.detail));
}

/* ── Retrieval & page grouping ──────────────────────────────────── */

int QueryWorker::countTokens(const QString &text) const {
  const int n =
      m_database ? m_database->vecManager().countTokens(text) : -1;
  return n >= 0 ? n : int(text.length() / 3) + 1;
}

QList<PageContext> QueryWorker::retrieveAndGroupPages(const QString &queryText,
                                                      int budgetTokens) const {
  if (!m_database || !m_database->isOpen()) {
    qWarning() << "[QueryWorker] database not open";
    return {};
  }

  // Enough candidates to fill the budget (chunks average ~350 tokens) with
  // room for the ones the relevance cut and the dedup will drop.
  const int k = qBound(80, budgetTokens / 140, 2000);
  QList<ChunkSearchResult> chunks =
      m_database->vecManager().search(m_database->db(), queryText, k);

  if (chunks.isEmpty()) {
    qDebug() << "[QueryWorker] no chunks found for query";
    return {};
  }

  // vec0 returns nearest first.
  const float best = chunks.first().distance;
  const float cutoff = best + RelevanceMargin;

  // ── Fill the budget by relevance, grouping chunks by (paper, page) ──
  QMap<QString, int> pageKeyToIndex; // "hash|page" → index in `pages`
  QList<PageContext> pages;

  QString imgPath = m_config->projectImgPath(m_config->lastProject());
  QDir imgDir(imgPath);
  const bool useVisual = m_config->useVisualContext();

  int used = 0;
  int images = 0;
  int kept = 0;
  int skippedBudget = 0;
  int cutByRelevance = 0;
  QList<float> keptDistances;

  for (qsizetype i = 0; i < chunks.size(); ++i) {
    const ChunkSearchResult &c = chunks[i];
    if (c.distance > cutoff) {
      // Sorted by distance: everything after is farther still.
      cutByRelevance = int(chunks.size() - i);
      break;
    }
    if (budgetTokens - used < 64)
      break; // budget full

    const int chunkCost = countTokens(c.text) + ChunkSeparatorTokens;
    const QString key = c.paperHash + "|" + QString::number(c.page);

    auto it = pageKeyToIndex.find(key);
    if (it != pageKeyToIndex.end()) {
      // Existing page — add the chunk if it is new and fits.
      PageContext &pc = pages[it.value()];
      if (pc.textChunks.contains(c.text))
        continue;
      if (used + chunkCost > budgetTokens) {
        ++skippedBudget;
        continue; // a smaller, less relevant chunk may still fit
      }
      pc.textChunks.append(c.text);
      used += chunkCost;
      ++kept;
      keptDistances << c.distance;
      continue;
    }

    // New page: header + chunk (+ image in visual mode, while it fits).
    PageContext pc;
    pc.paperHash = c.paperHash;
    pc.page = c.page;
    pc.cleanTitle = c.cleanTitle;
    pc.citation = c.citation;
    pc.textChunks.append(c.text);

    int cost = PageHeaderTokens + chunkCost;
    if (used + cost > budgetTokens) {
      ++skippedBudget;
      continue;
    }

    // Resolve image path: hash_page_NNN.png
    if (imgDir.exists()) {
      QString imgFile = QString("%1_page_%2.png")
                            .arg(c.paperHash)
                            .arg(displayPage(pc), 3, 10, QChar('0'));
      QString absoluteImg = imgDir.absoluteFilePath(imgFile);
      if (QFile::exists(absoluteImg))
        pc.imageFile = absoluteImg;
    }
    if (useVisual && !pc.imageFile.isEmpty() && images < MaxImages &&
        used + cost + ImageTokens <= budgetTokens) {
      pc.withImage = true;
      cost += ImageTokens;
      ++images;
    }

    pageKeyToIndex.insert(key, int(pages.size()));
    pages.append(pc);
    used += cost;
    ++kept;
    keptDistances << c.distance;
  }

  std::sort(keptDistances.begin(), keptDistances.end());
  qDebug() << "[QueryWorker] retrieved" << chunks.size() << "chunks (k =" << k
           << ") → kept" << kept << "in" << pages.size() << "pages," << images
           << "image(s) |" << used << "/" << budgetTokens << "tokens |"
           << cutByRelevance << "below relevance," << skippedBudget
           << "over budget";
  if (!keptDistances.isEmpty())
    qDebug() << "[QueryWorker] distances: best" << best << "| median"
             << keptDistances[keptDistances.size() / 2] << "| worst kept"
             << keptDistances.last() << "| cutoff" << cutoff;
  return pages;
}

/* ── Citations footer ───────────────────────────────────────────── */

QStringList
QueryWorker::buildCitationsFooter(const QList<PageContext> &pages,
                                  const QSet<int> &cited) const {
  // Only the papers the answer actually cites; every retrieved one if the
  // model ignored the reference tags.  Grouped by paper, in retrieval
  // (relevance) order.
  QStringList order;
  QHash<QString, QList<int>> pagesByPaper;
  QHash<QString, PaperLabel> labels;
  for (int i = 0; i < pages.size(); ++i) {
    if (!cited.isEmpty() && !cited.contains(i))
      continue;
    const PageContext &pc = pages[i];
    if (!pagesByPaper.contains(pc.paperHash)) {
      order.append(pc.paperHash);
      labels.insert(pc.paperHash, paperLabel(pc));
    }
    pagesByPaper[pc.paperHash].append(displayPage(pc));
  }

  QStringList citations;
  for (const QString &hash : order) {
    const PaperLabel &l = labels[hash];
    QString line = "- **" + TextFormat::escapeMarkdown(l.footerForm()) + "**";
    if (!l.title.isEmpty())
      line += " — *" + TextFormat::escapeMarkdown(l.title) + "*";
    line += " — " + pageRange(pagesByPaper.value(hash));
    citations.append(line);
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

    if (useVisual && pc.withImage) {
      QImage img(pc.imageFile);
      if (!img.isNull()) {
        pb.visualContext = PageImage::forModel(img);
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
  if (m_backend)
    m_backend->cancel();
  // Do NOT unload the chat model here: it may be held mid-llama_decode.
  // cleanupGpu() unloads it exactly once on the normal exit path.
  emit stopped();
  qDebug() << "[QueryWorker] Stop requested";
}

/* ── Database persistence ───────────────────────────────────────── */

std::expected<void, QString>
QueryWorker::saveQuery(const QString &queryText, const QString &response,
                       const QString &modelName) {
  if (!m_database || !m_database->isOpen()) {
    qWarning() << "[QueryWorker] Database not open — cannot save query";
    return std::unexpected(tr("the project database is not open"));
  }

  auto &db = m_database->db();

  SqliteStatement stmt =
      db.prepare("INSERT INTO Queries (query, response, llmodel, date) "
                 "VALUES (?, ?, ?, ?)");
  if (!stmt.isValid()) {
    qCritical() << "[QueryWorker] Failed to prepare query insert";
    return std::unexpected(tr("database error (prepare failed)"));
  }

  (void)stmt.bind(1, queryText);
  (void)stmt.bind(2, response);
  (void)stmt.bind(3, modelName);
  (void)stmt.bind(4, QDateTime::currentDateTimeUtc());

  auto execResult = stmt.exec();
  if (!execResult) {
    qCritical() << "[QueryWorker] Failed to save query:" << execResult.error();
    return std::unexpected(execResult.error());
  }

  if (m_database->queryModel())
    m_database->queryModel()->loadAll();

  qDebug() << "[QueryWorker] Saved query response (" << response.length()
           << "chars)";
  return {};
}
