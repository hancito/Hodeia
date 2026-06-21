#ifndef _Thread_local
#define _Thread_local thread_local
#endif
#ifndef LIBEMBEDDING_NO_IMAGE
#define LIBEMBEDDING_NO_IMAGE
#endif
#define LIBEMBEDDING_IMPLEMENTATION
#include <libembedding/libembedding.h>

#include "headers/sqlitevec.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstring>

namespace {
constexpr const char *EmbedPrefix = "Represent this sentence for retrieval: ";
}

/* ── Constructor / Destructor ────────────────────────────────── */

SqliteVecManager::SqliteVecManager(QObject *parent) : QObject(parent) {}

SqliteVecManager::~SqliteVecManager() {
  if (m_embeddingCtx) {
    lembed_text_embedding_free(
        static_cast<lembed_text_embedding_t *>(m_embeddingCtx));
    m_embeddingCtx = nullptr;
  }
}

/* ── Initialization ──────────────────────────────────────────── */

bool SqliteVecManager::initialize(const QString &cacheDir) {
  if (m_ready)
    return true;

  emit statusUpdated("Loading BGE Small EN v1.5 model...");

  lembed_text_options_t opts = lembed_text_options_default();
  opts.model = LEMBED_TEXT_BGE_SMALL_EN_V15;
  opts.provider = LEMBED_PROVIDER_CUDA;
  opts.device_id = 0;
  opts.max_length = 512;
  opts.num_threads = 0; // auto
  opts.show_download_progress = 0;

  QByteArray cacheDirUtf8;
  if (!cacheDir.isEmpty()) {
    cacheDirUtf8 = cacheDir.toUtf8();
    opts.cache_dir = cacheDirUtf8.constData();
  }

  lembed_text_embedding_t *ctx = nullptr;
  lembed_status_t status = lembed_text_embedding_create(&opts, &ctx);

  if (status != LEMBED_OK) {
    qCritical() << "[SqliteVec] Failed to create embedding model:"
                << lembed_status_message(status);
    emit statusUpdated(QString("Embedding model failed: %1")
                           .arg(lembed_status_message(status)));
    return false;
  }

  m_embeddingCtx = ctx;
  m_dim = lembed_text_embedding_dim(ctx);
  m_ready = true;

  qInfo() << "[SqliteVec] Model loaded, dim=" << m_dim;
  emit statusUpdated(
      QString("BGE Small EN v1.5 loaded (%1 dimensions)").arg(m_dim));
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

QVector<float> SqliteVecManager::embed(const QStringList &texts) {
  if (!m_ready || texts.isEmpty())
    return {};

  QStringList prefixedTexts;
  prefixedTexts.reserve(texts.size());
  for (const auto &t : texts) {
    prefixedTexts.append(QString::fromUtf8(EmbedPrefix) + t);
  }

  QVector<const char *> cstrs;
  cstrs.reserve(prefixedTexts.size());
  QVector<QByteArray> utf8Bufs;
  utf8Bufs.reserve(prefixedTexts.size());

  for (const auto &t : prefixedTexts) {
    QByteArray utf8 = t.toUtf8();
    utf8Bufs.append(utf8);
    cstrs.append(utf8Bufs.constLast().constData());
  }

  lembed_embeddings_t result;
  lembed_status_t status = lembed_text_embedding_embed(
      static_cast<lembed_text_embedding_t *>(m_embeddingCtx), cstrs.constData(),
      (int)prefixedTexts.size(), 0, &result);

  if (status != LEMBED_OK) {
    qCritical() << "[SqliteVec] Embed failed:" << lembed_status_message(status);
    return {};
  }

  QVector<float> embeddings(result.num_embeddings * result.dim);
  std::memcpy(embeddings.data(), result.data,
              (size_t)result.num_embeddings * result.dim * sizeof(float));
  lembed_embeddings_free(&result);

  return embeddings;
}

/* ── Chunk insertion ─────────────────────────────────────────── */

int SqliteVecManager::insertChunks(SqliteDb &db,
                                   const QList<TextChunk> &chunks) {
  if (!m_ready || chunks.isEmpty())
    return 0;

  int batchSize = 64;
  int totalInserted = 0;

  SqliteStatement stmt =
      db.prepare("INSERT INTO chunks (embedding, metadata_blob) "
                 "VALUES (vec_f32(?), ?)");
  if (!stmt.isValid()) {
    qCritical() << "[SqliteVec] Prepare failed:" << stmt.errorMsg();
    return -1;
  }

  for (int start = 0; start < chunks.size(); start += batchSize) {
    int end = qMin(start + batchSize, chunks.size());

    // Collect texts for this batch — embed all at once (GPU efficient)
    QStringList batchTexts;
    batchTexts.reserve(end - start);
    for (int j = start; j < end; ++j)
      batchTexts.append(chunks[j].text);

    QVector<float> allEmbeddings = embed(batchTexts);
    if (allEmbeddings.isEmpty()) {
      qWarning() << "[SqliteVec] Empty embeddings for batch starting at"
                 << start << ", skipping";
      return -1;
    }

    emit embeddingProgress(end, chunks.size());

    // Insert one-by-one so each metadata blob can be freed immediately
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
    // allEmbeddings destroyed here — memory freed before next batch
  }

  return totalInserted;
}

/* ── Search ──────────────────────────────────────────────────── */

QList<ChunkSearchResult> SqliteVecManager::search(SqliteDb &db,
                                                  const QString &query, int k) {
  if (!m_ready)
    return {};

  // Embed the query with the same prefix used for documents
  QString queryText = QString::fromUtf8(EmbedPrefix) + query;
  QVector<float> queryEmbedding = embed(QStringList{queryText});
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

/* ── Accessors ───────────────────────────────────────────────── */

int SqliteVecManager::embeddingDim() const { return m_dim; }
bool SqliteVecManager::isReady() const { return m_ready; }

void SqliteVecManager::shutdown() {
  if (m_embeddingCtx) {
    lembed_text_embedding_free(
        static_cast<lembed_text_embedding_t *>(m_embeddingCtx));
    m_embeddingCtx = nullptr;
  }
  m_ready = false;
  m_dim = 0;
  qInfo() << "[SqliteVec] Model unloaded";
}
