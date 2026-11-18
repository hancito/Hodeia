#include "queryworker.h"
#include "citationformat.h"
#include "configmanager.h"
#include "database.h"
#include "libraryrepository.h"
#include "metadataresolver.h"
#include "querypromptbuilder.h"
#include "retriever.h"
#include "sqlitevec.h"
#include "textformat.h"
#include "zoterorepository.h"

#include <QElapsedTimer>

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

  // 2. Retrieve & group pages from vector search.  The page context gets
  // what the system prompt, the instructions and the question leave of the
  // budget.
  QList<PageContext> pages;
  if (m_database && m_database->isOpen()) {
    const Retriever retriever(m_database->db(), m_database->vecManager());
    const int fixedTokens =
        retriever.countTokens(QueryPrompt::fixedText(queryText)) + 64;
    Retriever::Options options;
    options.budgetTokens = qMax(1024, contextTokens - fixedTokens);
    options.useVisual = m_config->useVisualContext();
    options.imageDir = m_config->projectImgPath(m_config->lastProject());
    qDebug() << "[QueryWorker] Starting query:" << queryText.left(60)
             << "| context:" << contextTokens << "tokens | page budget:"
             << options.budgetTokens;
    pages = retriever.retrieve(queryText, options);
    // Zotero's metadata name the papers in the prompt and the sources.
    if (m_config->zoteroPriority())
      MetadataResolver::attachZotero(
          pages, ZoteroRepository(m_database->db()).metaByHash());
  } else {
    qWarning() << "[QueryWorker] database not open";
  }
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

  // 3. Build prompt (the embedding model stays loaded: vector search is
  // done, and a query is a single generation).
  PromptPayload payload =
      QueryPrompt::build(pages, queryText, m_config->useVisualContext());
  qDebug() << "[QueryWorker] Prompt built — text context:"
           << payload.textContext.length()
           << "chars, visuals:" << payload.visualContext.size();

  // 4. Run inference
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
    result = CitationFormat::resolveReferenceTags(result, pages, cited);
    result += CitationFormat::sourcesSection(pages, cited);
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
  // releaseBackend() unloads it exactly once on the normal exit path.
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

  if (auto saved = LibraryRepository(m_database->db())
                       .saveQuery(queryText, response, modelName);
      !saved)
    return saved;
  // The history model is reloaded on the GUI thread (QueryController), not
  // here: this runs on the worker thread.
  return {};
}
