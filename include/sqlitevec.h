#pragma once

#include "llama.h"
#include "sqlitedb.h"
#include "textchunk.h"
#include <QList>
#include <QMutex>
#include <QReadWriteLock>
#include <QString>
#include <QVector>
#include <atomic>
#include <expected>

struct ChunkSearchResult {
  QString paperHash;
  int page;
  int chunkIdx;
  QString text;
  float distance;
  QString citation;
  QString cleanTitle;
  QString pageMetadata; ///< JSON: figures, tables, scanned flags
};

class ConfigManager;

class SqliteVecManager : public QObject {
  Q_OBJECT

public:
  enum class EmbedMode { Passage, Query };

  explicit SqliteVecManager(ConfigManager *config, QObject *parent = nullptr);
  ~SqliteVecManager() override;

  // Initialize the embedding model via llama.cpp.
  // modelPath points to a GGUF embedding model file.
  // Empty string uses the default E5 model in Models/Embedders/.
  bool initialize(const QString &modelPath = QString());

  // Embed a batch of text strings; returns flat float vectors (rows × dims).
  // Mode controls the E5 prefix ("passage: " vs "query: ").
  QVector<float> embed(const QStringList &texts,
                       EmbedMode mode = EmbedMode::Passage);

  // Token count of `text` with the embedding model's own tokenizer, without
  // the E5 prefix.  Loads the model on first use; -1 if it can't be loaded
  // or the tokenizer fails (no tokens for a non-blank text).
  int countTokens(const QString &text);

  // Largest chunk (in tokens, prefix excluded) that the embedding model can
  // read without truncation: its context minus the "passage: " prefix and a
  // safety margin.  0 if the model can't be loaded.
  int maxChunkTokens();

  // Embed the chunks' texts as passages, without touching any database: one
  // float32 blob per chunk, in order (what LibraryRepository stores).  Empty
  // on failure or when `cancel` becomes true (checked between batches).
  QList<QByteArray> embedPassages(const QList<TextChunk> &chunks,
                                  const std::atomic<bool> *cancel = nullptr);

  // Search chunks by query text; returns the top-k nearest neighbors, nearest
  // first.  With `paperHashes`, only the chunks of those papers.
  QList<ChunkSearchResult> search(SqliteDb &db, const QString &query,
                                  int k = 10,
                                  const QStringList &paperHashes = {});

  // Retrieve all chunks belonging to a paper by its MD5 hash.
  // Returns ChunkSearchResult items (distance is set to 0.0f).
  QList<ChunkSearchResult> getChunksByHash(SqliteDb &db,
                                           const QString &paperHash);

  // Get embedding dimensionality of the loaded model
  int embeddingDim() const;

  // Whether the embedding model is loaded and ready
  bool isReady() const;

  // User-facing reason of the last failed initialize(); empty once the
  // model loads.  Lets workers explain why nothing could be embedded.
  QString lastError() const;

  // Release the embedding model and free GPU/CPU memory
  void shutdown();

signals:
  void statusUpdated(const QString &msg);
  void embeddingProgress(int current, int total);

private:
  struct llama_context *m_llamaCtx = nullptr;
  struct llama_model *m_llamaModel = nullptr;
  bool m_ready = false;
  int m_dim = 0;
  int m_maxTokens = 0;
  ConfigManager *m_config = nullptr;

  // Serializes all access to the llama context/model. embed() is called
  // concurrently from the ingestion and query worker threads, and
  // prepareGpu/cleanupGpu call initialize()/shutdown() from workers; without
  // this, concurrent llama_encode on one context corrupts the KV cache and
  // llama_free under a live encode is a segfault.
  QMutex m_lock;
  // Guards the model's lifetime: written by initialize()/shutdown(), read
  // by embed() (before m_lock) and by countTokens(), which only reads the
  // model's vocabulary (thread-safe) and so does not take m_lock.
  QReadWriteLock m_modelLock;

  void setLastError(const QString &error);
  QString m_lastError;
  mutable QMutex m_errorLock; // guards m_lastError (read from any thread)
};
