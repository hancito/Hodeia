#include "sqlitevec.h"
#include "configmanager.h"

#include "llama.h"
#include "llamacpp/common/common.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QMutexLocker>
#include <QReadLocker>
#include <QWriteLocker>
#include <Sqlite3/sqlite3.h>
#include <algorithm>
#include <atomic>
#include <expected>
#include <mutex>

namespace {
constexpr const char *PassagePrefix = "passage: ";
constexpr const char *QueryPrefix = "query: ";
constexpr int BatchSize = 64;
} // namespace

/* ── Constructor / Destructor ────────────────────────────────── */

SqliteVecManager::SqliteVecManager(ConfigManager *config, QObject *parent)
    : QObject(parent), m_config(config) {}

SqliteVecManager::~SqliteVecManager() { shutdown(); }

/* ── Initialization ──────────────────────────────────────────── */

static QString resolveModelPath(ConfigManager *config,
                                const QString &modelPath) {
  QString path = modelPath;
  if (path.isEmpty() && config) {
    path = config->embeddersPath() + "/multilingual-e5-small-q8_0.gguf";
  }

  if (path.isEmpty())
    return QString();

  if (QFile::exists(path))
    return path;

  QString appDir = QCoreApplication::applicationDirPath();
  QString candidate = appDir + "/" + path;
  if (QFile::exists(candidate))
    return candidate;

  return path;
}

bool SqliteVecManager::initialize(const QString &modelPath) {
  if (m_ready)
    return true;

  QString resolvedPath = resolveModelPath(m_config, modelPath);

  if (!QFile::exists(resolvedPath)) {
    qCritical() << "[SqliteVec] Model file not found:" << resolvedPath;
    setLastError(tr("Embedding model file not found: %1").arg(resolvedPath));
    emit statusUpdated(
        tr("Embedding model file not found: %1").arg(resolvedPath));
    return false;
  }

  emit statusUpdated(tr("Loading embedding model: %1").arg(resolvedPath));

  // All llama runtime + model/context setup is guarded by m_modelLock
  // (written) and m_lock so that concurrent initialize()/embed()/shutdown()
  // calls from worker threads cannot interleave on the same context.
  const QWriteLocker modelLocker(&m_modelLock);
  const QMutexLocker locker(&m_lock);
  if (m_ready) // loaded by another thread meanwhile
    return true;

  // Init llama backends once.  No common_init(): it calls
  // llama_log_set(common_log_default_callback), which would replace the
  // filtered log callback installed by main.cpp (initLlamaLogging).
  static std::once_flag s_initFlag;
  std::call_once(s_initFlag, []() {
    ggml_backend_load_all();
    llama_backend_init();
  });

  QByteArray pathUtf8 = resolvedPath.toUtf8();

  llama_model_params mparams = llama_model_default_params();

  auto *model = llama_model_load_from_file(pathUtf8.constData(), mparams);
  if (!model) {
    qCritical() << "[SqliteVec] Failed to load model:" << resolvedPath;
    setLastError(tr("Failed to load embedding model: %1").arg(resolvedPath));
    emit statusUpdated(
        tr("Failed to load embedding model: %1").arg(resolvedPath));
    return false;
  }

  llama_context_params cparams = llama_context_default_params();
  // Use the model's actual training context to avoid out-of-bounds
  // position-embedding access (E5 is trained on 512 = 511 + BOS).
  int32_t ctxTrain = llama_model_n_ctx_train(model);
  uint32_t safeCtx = (ctxTrain > 0) ? static_cast<uint32_t>(ctxTrain)
                                    : static_cast<uint32_t>(511);
  cparams.n_ctx = safeCtx;
  cparams.n_batch = safeCtx;
  cparams.n_ubatch = safeCtx;
  cparams.n_seq_max = 1; // we embed one passage at a time
  cparams.embeddings = true;
  cparams.pooling_type = LLAMA_POOLING_TYPE_MEAN;
  cparams.n_threads = common_cpu_get_num_physical_cores();
  cparams.n_threads_batch = common_cpu_get_num_physical_cores();

  auto *ctx = llama_init_from_model(model, cparams);
  if (!ctx) {
    llama_model_free(model);
    qCritical() << "[SqliteVec] Failed to create context";
    setLastError(tr("The embedding model could not be started (not enough "
                    "memory?)"));
    emit statusUpdated(tr("Failed to create llama context"));
    return false;
  }

  m_llamaModel = model;
  m_llamaCtx = ctx;
  m_dim = llama_model_n_embd(model);
  m_maxTokens = static_cast<int>(safeCtx);
  m_ready = true;
  setLastError(QString());

  qInfo() << "[SqliteVec] E5 model loaded, dim=" << m_dim
          << "| maxTokens=" << m_maxTokens;
  emit statusUpdated(
      tr("E5 embedding model loaded (%1 dimensions)").arg(m_dim));
  return true;
}

/* ── Embedding ───────────────────────────────────────────────── */

QVector<float> SqliteVecManager::embed(const QStringList &texts,
                                       EmbedMode mode) {
  if (texts.isEmpty())
    return {};
  if (!m_ready && !initialize())
    return {};
  if (!m_ready)
    return {};

  // Serialize on the llama context. initialize() has already released the
  // locks, so it is safe to re-lock here (QMutex is non-recursive but not
  // held).  m_modelLock first: the model stays loaded meanwhile.
  const QReadLocker modelLocker(&m_modelLock);
  const QMutexLocker locker(&m_lock);
  if (!m_llamaCtx)
    return {};

  auto *ctx = m_llamaCtx;
  auto *vocab = llama_model_get_vocab(m_llamaModel);

  const char *prefix = (mode == EmbedMode::Query) ? QueryPrefix : PassagePrefix;

  QVector<float> results;
  results.reserve((int)texts.size() * m_dim);

  for (const auto &text : texts) {
    QString prefixed = QString::fromUtf8(prefix) + text;
    QByteArray utf8 = prefixed.toUtf8();

    // The model's special tokens (<s> … </s> for multilingual E5) frame the
    // text as in training; without them the embeddings are much less
    // discriminating (a paper's exact title did not find the paper).
    // Changing this changes every vector: the stored chunks must then be
    // embedded again (a migration, see ProjectStore v3 and Reindexer).
    constexpr bool addSpecial = true;
    auto tokens =
        common_tokenize(vocab, std::string(utf8.constData()),
                        /*add_special=*/addSpecial, /*parse_special=*/true);

    if (tokens.empty()) {
      // Never legitimate: the E5 prefix alone yields tokens.  Seen when the
      // app's C++ runtime doesn't match llama-common.dll (Debug /MDd build
      // against the Release DLLs): common_tokenize silently returns nothing.
      // Fail the whole call rather than store zero vectors that make every
      // similarity search meaningless.
      qCritical() << "[SqliteVec] Tokenizer returned no tokens for a"
                  << prefixed.length()
                  << "char text — embedding aborted (C++ runtime mismatch "
                     "with llama-common? use a Release/RelWithDebInfo build)";
      return {};
    }

    // Truncate to context window, keeping the closing special token.
    if ((int)tokens.size() > m_maxTokens) {
      const llama_token last = tokens.back();
      tokens.resize(m_maxTokens);
      if (addSpecial)
        tokens.back() = last;
    }

    // Build the batch using llama_batch_init (heap-allocated, safe).
    auto batch_tokens = std::min((int32_t)tokens.size(), (int32_t)m_maxTokens);
    struct llama_batch batch =
        llama_batch_init(batch_tokens, /*embd=*/0, /*n_seq_max=*/1);
    batch.n_tokens = batch_tokens;
    for (int32_t i = 0; i < batch_tokens; i++) {
      batch.token[i] = tokens[i];
      batch.pos[i] = i;
      batch.n_seq_id[i] = 1;
      batch.seq_id[i][0] = 0;
      batch.logits[i] = 1; // mark all tokens as outputs for embedding mode
    }

    // E5 is an encoder-only (BERT) model — use llama_encode, not llama_decode
    if (llama_encode(ctx, batch) != 0) {
      llama_batch_free(batch);
      qWarning() << "[SqliteVec] llama_encode failed";
      for (int d = 0; d < m_dim; ++d)
        results.append(0.0f);
      continue;
    }
    llama_batch_free(batch);

    float *embd_raw = llama_get_embeddings_seq(ctx, 0);
    if (!embd_raw) {
      qWarning() << "[SqliteVec] No embeddings available for sequence";
      for (int d = 0; d < m_dim; ++d)
        results.append(0.0f);
      continue;
    }

    // L2-normalize the raw mean-pooled embedding (required by E5 for correct
    // cosine similarity)
    QVector<float> normalized(m_dim);
    common_embd_normalize(embd_raw, normalized.data(), m_dim,
                          /*embd_norm=*/2); // 2 = L2 / euclidean
    results.append(normalized);

    // Free the memory/KV cache slot — encoder models accumulate slots on
    // each llama_encode call and leak VRAM if never cleared.
    auto mem = llama_get_memory(ctx);
    if (mem)
      llama_memory_clear(mem, true);
  }

  return results;
}

/* ── Token counting ──────────────────────────────────────────── */

int SqliteVecManager::countTokens(const QString &text) {
  if (!m_ready && !initialize())
    return -1;
  // The vocabulary only (no context): not m_lock, so the chunking of the
  // extraction threads does not wait for embed() to finish its batch.
  const QReadLocker modelLocker(&m_modelLock);
  if (!m_llamaModel)
    return -1;
  const auto tokens =
      common_tokenize(llama_model_get_vocab(m_llamaModel),
                      text.toStdString(), /*add_special=*/false,
                      /*parse_special=*/false);
  if (tokens.empty() && !text.trimmed().isEmpty()) {
    // A non-blank text always yields tokens: this is a tokenizer failure
    // (e.g. C++ runtime mismatch with llama-common.dll).  Report "unknown"
    // so callers fall back to their character estimate instead of counting
    // 0 — which once packed whole papers into a single chunk.
    static std::atomic_bool warned{false};
    if (!warned.exchange(true))
      qCritical() << "[SqliteVec] Tokenizer returned no tokens for non-empty "
                     "text — falling back to character estimates (C++ "
                     "runtime mismatch with llama-common? use a "
                     "Release/RelWithDebInfo build)";
    return -1;
  }
  return static_cast<int>(tokens.size());
}

int SqliteVecManager::maxChunkTokens() {
  const int prefixTokens = countTokens(QString::fromUtf8(PassagePrefix));
  if (prefixTokens < 0)
    return 0;
  // Margin: tokenizing a whole chunk can merge/split differently than the
  // sum of its sentences, and the encoder may add special tokens.
  constexpr int SafetyMargin = 16;
  return qMax(64, m_maxTokens - prefixTokens - SafetyMargin);
}

/* ── Chunk insertion ─────────────────────────────────────────── */

QList<QByteArray>
SqliteVecManager::embedPassages(const QList<TextChunk> &chunks,
                                const std::atomic<bool> *cancel) {
  if (chunks.isEmpty())
    return {};
  if (!m_ready && !initialize())
    return {};
  if (!m_ready)
    return {};

  QElapsedTimer timer; // for the log at the end
  timer.start();
  QList<QByteArray> embeddings;
  embeddings.reserve(chunks.size());

  for (int start = 0; start < chunks.size(); start += BatchSize) {
    if (cancel && cancel->load()) {
      qInfo() << "[SqliteVec] embedPassages cancelled";
      return {};
    }
    const int end = qMin(start + BatchSize, chunks.size());

    QStringList batchTexts;
    batchTexts.reserve(end - start);
    for (int j = start; j < end; ++j)
      batchTexts.append(chunks[j].text);

    const QVector<float> batch = embed(batchTexts, EmbedMode::Passage);
    if (batch.size() != (end - start) * m_dim) {
      qWarning() << "[SqliteVec] Embedding failed for the batch starting at"
                 << start;
      return {};
    }
    for (int i = 0; i < end - start; ++i)
      embeddings.append(
          QByteArray(reinterpret_cast<const char *>(batch.constData() +
                                                    i * m_dim),
                     m_dim * qsizetype(sizeof(float))));

    emit embeddingProgress(end, chunks.size());
  }

  qInfo().noquote() << QString("[SqliteVec] Embedded %1 chunks in %2 ms")
                           .arg(chunks.size())
                           .arg(timer.elapsed());
  return embeddings;
}

/* ── Search ──────────────────────────────────────────────────── */

QList<ChunkSearchResult> SqliteVecManager::search(SqliteDb &db,
                                                  const QString &query, int k,
                                                  const QStringList &paperHashes) {
  if (!m_ready && !initialize())
    return {};
  if (!m_ready)
    return {};

  QVector<float> queryEmbedding = embed(QStringList{query}, EmbedMode::Query);
  if (queryEmbedding.isEmpty())
    return {};

  QByteArray queryBlob(
      reinterpret_cast<const char *>(queryEmbedding.constData()),
      m_dim * sizeof(float));

  // One KNN query per paper (vec0 filters on a metadata column with '='),
  // merged by distance.
  const QStringList filters = paperHashes.isEmpty() ? QStringList{QString()}
                                                    : paperHashes;
  QList<ChunkSearchResult> results;
  for (const QString &hash : filters) {
    QString sql =
        QString("SELECT paper_hash, page, chunk_idx, chunk_text, citation, "
                "clean_title, page_metadata, distance "
                "FROM chunks WHERE embedding MATCH vec_f32(?)%1 "
                "LIMIT %2")
            .arg(hash.isEmpty() ? QString() : QStringLiteral(" AND paper_hash = ?"))
            .arg(k);

    SqliteStatement stmt = db.prepare(sql);
    if (!stmt.isValid()) {
      qCritical() << "[SqliteVec] Prepare failed:"
                  << QString::fromUtf8(sqlite3_errmsg(db.handle()));
      return {};
    }

    auto bindResult = stmt.bind(1, queryBlob);
    if (bindResult && !hash.isEmpty())
      bindResult = stmt.bind(2, hash);
    if (!bindResult) {
      qCritical() << "[SqliteVec] Bind failed:" << bindResult.error();
      return {};
    }

    while (true) {
      auto stepResult = stmt.step();
      if (!stepResult) {
        qCritical() << "[SqliteVec] Step failed:" << stepResult.error();
        break;
      }
      if (!*stepResult)
        break;
      ChunkSearchResult r;
      r.paperHash = stmt.columnText(0);
      r.page = stmt.columnInt(1);
      r.chunkIdx = stmt.columnInt(2);
      r.text = stmt.columnText(3);
      r.citation = stmt.columnText(4);
      r.cleanTitle = stmt.columnText(5);
      r.pageMetadata = stmt.columnText(6);
      r.distance = stmt.columnDouble(7);
      results.append(r);
    }
  }

  if (filters.size() > 1) {
    std::stable_sort(results.begin(), results.end(),
                     [](const ChunkSearchResult &a, const ChunkSearchResult &b) {
                       return a.distance < b.distance;
                     });
    if (results.size() > k)
      results.resize(k);
  }
  return results;
}

/* ── Get chunks by paper hash ─────────────────────────────────── */

QList<ChunkSearchResult>
SqliteVecManager::getChunksByHash(SqliteDb &db, const QString &paperHash) {
  if (paperHash.isEmpty())
    return {};

  QString sql = "SELECT paper_hash, page, chunk_idx, chunk_text, citation, "
                "clean_title, page_metadata FROM chunks WHERE paper_hash = ? "
                "ORDER BY page, chunk_idx";

  SqliteStatement stmt = db.prepare(sql);
  if (!stmt.isValid())
    return {};

  auto bindResult = stmt.bind(1, paperHash);
  if (!bindResult) {
    qCritical() << "[SqliteVec] Bind failed:" << bindResult.error();
    return {};
  }

  QList<ChunkSearchResult> results;
  while (true) {
    auto stepResult = stmt.step();
    if (!stepResult) {
      qCritical() << "[SqliteVec] Step failed:" << stepResult.error();
      break;
    }
    if (!*stepResult)
      break;
    ChunkSearchResult r;
    r.paperHash = stmt.columnText(0);
    r.page = stmt.columnInt(1);
    r.chunkIdx = stmt.columnInt(2);
    r.text = stmt.columnText(3);
    r.citation = stmt.columnText(4);
    r.cleanTitle = stmt.columnText(5);
    r.pageMetadata = stmt.columnText(6);
    r.distance = 0.0f;
    results.append(r);
  }

  int totalChars = 0;
  for (const auto &r : results)
    totalChars += r.text.length();
  qDebug() << "[SqliteVec] getChunksByHash:" << paperHash.left(12) << "→"
           << results.size() << "chunks," << totalChars << "chars total";

  return results;
}

/* ── Accessors ───────────────────────────────────────────────── */

int SqliteVecManager::embeddingDim() const { return m_dim; }
bool SqliteVecManager::isReady() const { return m_ready; }

QString SqliteVecManager::lastError() const {
  const QMutexLocker locker(&m_errorLock);
  return m_lastError;
}

void SqliteVecManager::setLastError(const QString &error) {
  const QMutexLocker locker(&m_errorLock);
  m_lastError = error;
}

void SqliteVecManager::shutdown() {
  const QWriteLocker modelLocker(&m_modelLock);
  const QMutexLocker locker(&m_lock);
  if (m_llamaCtx) {
    llama_free(m_llamaCtx);
    m_llamaCtx = nullptr;
  }
  if (m_llamaModel) {
    llama_model_free(m_llamaModel);
    m_llamaModel = nullptr;
  }
  m_ready = false;
  m_dim = 0;
  m_maxTokens = 0;
  qInfo() << "[SqliteVec] Model unloaded";
}
