#pragma once

#include "headers/summary.h"
#include <QList>
#include <QString>
#include <QVector>

struct ChunkSearchResult {
  QString paperHash;
  int page;
  int chunkIdx;
  QString text;
  float distance;
};

class SqliteVecManager : public QObject {
  Q_OBJECT

public:
  enum class EmbedMode { Passage, Query };

  explicit SqliteVecManager(QObject *parent = nullptr);
  ~SqliteVecManager() override;

  // Initialize the embedding model via llama.cpp.
  // modelPath points to a GGUF embedding model file.
  // Empty string uses the default E5 model in Models/Embedders/.
  bool initialize(const QString &modelPath = QString());

  // Create the vec0 virtual table "chunks" in the given database
  bool createChunksTable(SqliteDb &db);

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
  bool m_ready = false;
  int m_dim = 0;
  void *m_llamaCtx = nullptr;   // llama_context*
  void *m_llamaModel = nullptr; // llama_model*
  QString m_modelPath;
};
