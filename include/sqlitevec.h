#pragma once

#include "llama.h"
#include "summary.h"
#include <QList>
#include <QMutex>
#include <QString>
#include <QVector>
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

  // Create the vec0 virtual table "chunks" in the given database
  std::expected<void, QString> createChunksTable(SqliteDb &db);

  // Embed a batch of text strings; returns flat float vectors (rows × dims).
  // Mode controls the E5 prefix ("passage: " vs "query: ").
  QVector<float> embed(const QStringList &texts,
                       EmbedMode mode = EmbedMode::Passage);

  // Insert chunks into the vec0 table. Returns count of inserted rows.
  int insertChunks(SqliteDb &db, const QList<TextChunk> &chunks);

  // Search chunks by query text; returns top-k nearest neighbors
  QList<ChunkSearchResult> search(SqliteDb &db, const QString &query,
                                  int k = 10);

  // Retrieve all chunks belonging to a paper by its MD5 hash.
  // Returns ChunkSearchResult items (distance is set to 0.0f).
  QList<ChunkSearchResult> getChunksByHash(SqliteDb &db,
                                           const QString &paperHash);

  // Get embedding dimensionality of the loaded model
  int embeddingDim() const;

  // Whether the embedding model is loaded and ready
  bool isReady() const;

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
};
