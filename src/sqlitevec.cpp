#include "headers/sqlitevec.h"
#include "headers/configmanager.h"

#include "llama.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <algorithm>

namespace {
constexpr const char *PassagePrefix = "passage: ";
constexpr const char *QueryPrefix = "query: ";
constexpr int32_t MaxSeqLen = 512;
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
    path = config->embeddersPath() + "/multilingual-e5-small.gguf";
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
    emit statusUpdated("Embedding model file not found: " + resolvedPath);
    return false;
  }

  emit statusUpdated(QString("Loading embedding model: %1").arg(resolvedPath));

  QByteArray pathUtf8 = resolvedPath.toUtf8();

  llama_model_params mparams = llama_model_default_params();
  mparams.vocab_only = false;

  auto *model = llama_model_load_from_file(pathUtf8.constData(), mparams);
  if (!model) {
    qCritical() << "[SqliteVec] Failed to load model:" << resolvedPath;
    emit statusUpdated(
        QString("Failed to load embedding model: %1").arg(resolvedPath));
    return false;
  }

  llama_context_params cparams = llama_context_default_params();
  cparams.n_ctx = (uint32_t)MaxSeqLen;
  cparams.n_batch = (uint32_t)MaxSeqLen;
  cparams.embeddings = true;
  cparams.pooling_type = LLAMA_POOLING_TYPE_MEAN;
  cparams.n_threads = QThread::idealThreadCount();
  cparams.n_threads_batch = QThread::idealThreadCount();

  auto *ctx = llama_init_from_model(model, cparams);
  if (!ctx) {
    llama_model_free(model);
    qCritical() << "[SqliteVec] Failed to create context";
    emit statusUpdated("Failed to create llama context");
    return false;
  }

  m_llamaModel = model;
  m_llamaCtx = ctx;
  m_dim = llama_model_n_embd(model);
  m_ready = true;

  qInfo() << "[SqliteVec] E5 model loaded, dim=" << m_dim;
  emit statusUpdated(
      QString("E5 embedding model loaded (%1 dimensions)").arg(m_dim));
  return true;
}

/* ── vec0 table creation ─────────────────────────────────────── */

bool SqliteVecManager::createChunksTable(SqliteDb &db) {
  QString sql = QString("CREATE VIRTUAL TABLE IF NOT EXISTS chunks USING vec0("
                        "embedding float[%1], "
                        "metadata_blob text)")
                    .arg(m_dim > 0 ? m_dim : 384);
  return db.exec(sql);
}

/* ── Embedding ───────────────────────────────────────────────── */

QVector<float> SqliteVecManager::embed(const QStringList &texts,
                                       EmbedMode mode) {
  if (!m_ready || !m_llamaCtx || !m_llamaModel || texts.isEmpty())
    return {};

  auto *ctx = static_cast<struct llama_context *>(m_llamaCtx);
  auto *model = static_cast<struct llama_model *>(m_llamaModel);
  auto *vocab = llama_model_get_vocab(model);

  const char *prefix = (mode == EmbedMode::Query) ? QueryPrefix : PassagePrefix;

  QVector<float> results;
  results.reserve((int)texts.size() * m_dim);

  for (const auto &text : texts) {
    QString prefixed = QString::fromUtf8(prefix) + text;
    QByteArray utf8 = prefixed.toUtf8();

    std::vector<llama_token> tokens(MaxSeqLen);
    // First pass: discover how many tokens are needed.
    // llama_tokenize returns -N when the output buffer is too small.
    int32_t nTok = llama_tokenize(
        vocab, utf8.constData(), (int32_t)utf8.size(), nullptr, 0,
        /*add_special=*/false, /*parse_special=*/true);
    if (nTok < 0)
      nTok = -nTok; // flip to positive count
    if (nTok <= 0) {
      // Empty or whitespace-only text — embed as zero vector
      results.resize(m_dim);
      std::fill(results.begin(), results.end(), 0.0f);
      continue;
    }
    // Truncate to context window
    nTok = qMin(nTok, MaxSeqLen);
    // Second pass: actually tokenize into the buffer
    int32_t written = llama_tokenize(
        vocab, utf8.constData(), (int32_t)utf8.size(), tokens.data(), nTok,
        /*add_special=*/false, /*parse_special=*/true);
    if (written < 0)
      written = -written;
    nTok = qMin(nTok, written);
    if (nTok <= 0) {
      qWarning() << "[SqliteVec] Tokenization produced 0 tokens for text len"
                 << utf8.size();
      results.resize(m_dim);
      std::fill(results.begin(), results.end(), 0.0f);
      continue;
    }

    llama_batch batch = llama_batch_get_one(tokens.data(), nTok);

    // E5 is an encoder-only (BERT) model — use llama_encode, not llama_decode
    if (llama_encode(ctx, batch) != 0) {
      qWarning() << "[SqliteVec] llama_encode failed";
      results.resize(m_dim);
      std::fill(results.begin(), results.end(), 0.0f);
      continue;
    }
    // NOTE: do NOT call llama_batch_free() — llama_batch_get_one uses stack
    // memory

    float *embd = llama_get_embeddings_seq(ctx, 0);
    if (!embd) {
      qWarning() << "[SqliteVec] No embeddings available for sequence";
      results.resize(m_dim);
      std::fill(results.begin(), results.end(), 0.0f);
      continue;
    }
    for (int d = 0; d < m_dim; ++d)
      results.append(embd[d]);
  }

  return results;
}

/* ── Chunk insertion ─────────────────────────────────────────── */

int SqliteVecManager::insertChunks(SqliteDb &db,
                                   const QList<TextChunk> &chunks) {
  if (!m_ready || chunks.isEmpty())
    return 0;

  int totalInserted = 0;

  SqliteStatement stmt =
      db.prepare("INSERT INTO chunks (embedding, metadata_blob) "
                 "VALUES (vec_f32(?), ?)");
  if (!stmt.isValid()) {
    qCritical() << "[SqliteVec] Prepare failed:" << stmt.errorMsg();
    return -1;
  }

  for (int start = 0; start < chunks.size(); start += BatchSize) {
    int end = qMin(start + BatchSize, chunks.size());

    QStringList batchTexts;
    batchTexts.reserve(end - start);
    for (int j = start; j < end; ++j)
      batchTexts.append(chunks[j].text);

    QVector<float> allEmbeddings = embed(batchTexts, EmbedMode::Passage);
    if (allEmbeddings.isEmpty()) {
      qWarning() << "[SqliteVec] Empty embeddings for batch starting at"
                 << start << ", skipping";
      return -1;
    }

    emit embeddingProgress(end, chunks.size());

    for (int i = 0; i < end - start; ++i) {
      const TextChunk &c = chunks[start + i];
      const float *rowPtr = allEmbeddings.constData() + i * m_dim;
      QByteArray blob(reinterpret_cast<const char *>(rowPtr),
                      m_dim * sizeof(float));

      QJsonObject meta;
      meta["paper_hash"] = c.paperHash;
      meta["page"] = c.page;
      meta["chunk_idx"] = c.chunkIdx;
      meta["text"] = c.text;

      QByteArray metaJson = QJsonDocument(meta).toJson(QJsonDocument::Compact);

      stmt.bind(1, blob);
      stmt.bind(2, QString::fromUtf8(metaJson));

      if (!stmt.exec()) {
        qCritical() << "[SqliteVec] Insert failed:" << stmt.errorMsg();
        return -1;
      }
      stmt.resetAndClearBindings();
      totalInserted++;
    }
  }

  return totalInserted;
}

/* ── Search ──────────────────────────────────────────────────── */

QList<ChunkSearchResult> SqliteVecManager::search(SqliteDb &db,
                                                  const QString &query, int k) {
  if (!m_ready)
    return {};

  QVector<float> queryEmbedding = embed(QStringList{query}, EmbedMode::Query);
  if (queryEmbedding.isEmpty())
    return {};

  QByteArray queryBlob(
      reinterpret_cast<const char *>(queryEmbedding.constData()),
      m_dim * sizeof(float));

  QString sql = QString("SELECT metadata_blob, distance "
                        "FROM chunks WHERE embedding MATCH vec_f32(?) "
                        "ORDER BY distance LIMIT %1")
                    .arg(k);

  SqliteStatement stmt = db.prepare(sql);
  if (!stmt.isValid())
    return {};

  stmt.bind(1, queryBlob);

  QList<ChunkSearchResult> results;
  while (stmt.step()) {
    QString metaStr = stmt.columnText(0);
    float distance = stmt.columnDouble(1);

    QJsonObject meta = QJsonDocument::fromJson(metaStr.toUtf8()).object();
    ChunkSearchResult r;
    r.paperHash = meta.value("paper_hash").toString();
    r.page = meta.value("page").toInt();
    r.chunkIdx = meta.value("chunk_idx").toInt();
    r.text = meta.value("text").toString();
    r.distance = distance;
    results.append(r);
  }

  return results;
}

/* ── Get chunks by paper hash ─────────────────────────────────── */

QList<ChunkSearchResult>
SqliteVecManager::getChunksByHash(SqliteDb &db, const QString &paperHash) {
  if (paperHash.isEmpty())
    return {};

  // The paper_hash lives inside the JSON metadata_blob.
  // vec0 does not support indexed WHERE on text columns, so we
  // scan with a LIKE filter and verify the key in C++ to avoid
  // false positives from other JSON keys containing the same substring.
  QString sql = "SELECT metadata_blob FROM chunks WHERE metadata_blob LIKE ?";

  SqliteStatement stmt = db.prepare(sql);
  if (!stmt.isValid())
    return {};

  stmt.bind(1, QString("\"paper_hash\":\"%1\"").arg(paperHash));

  QList<ChunkSearchResult> results;
  while (stmt.step()) {
    QString metaStr = stmt.columnText(0);

    QJsonObject meta = QJsonDocument::fromJson(metaStr.toUtf8()).object();
    if (meta.value("paper_hash").toString() != paperHash)
      continue; // guard against LIKE false positives

    ChunkSearchResult r;
    r.paperHash = meta.value("paper_hash").toString();
    r.page = meta.value("page").toInt();
    r.chunkIdx = meta.value("chunk_idx").toInt();
    r.text = meta.value("text").toString();
    r.distance = 0.0f;
    results.append(r);
  }

  // Sort by page then chunk_idx for deterministic, readable order
  std::sort(results.begin(), results.end(),
            [](const ChunkSearchResult &a, const ChunkSearchResult &b) {
              if (a.page != b.page)
                return a.page < b.page;
              return a.chunkIdx < b.chunkIdx;
            });

  return results;
}

/* ── Accessors ───────────────────────────────────────────────── */

int SqliteVecManager::embeddingDim() const { return m_dim; }
bool SqliteVecManager::isReady() const { return m_ready; }

void SqliteVecManager::shutdown() {
  if (m_llamaCtx) {
    llama_free(static_cast<struct llama_context *>(m_llamaCtx));
    m_llamaCtx = nullptr;
  }
  if (m_llamaModel) {
    llama_model_free(static_cast<struct llama_model *>(m_llamaModel));
    m_llamaModel = nullptr;
  }
  m_ready = false;
  m_dim = 0;
  qInfo() << "[SqliteVec] Model unloaded";
}
