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
  explicit SqliteVecManager(QObject *parent = nullptr);
  ~SqliteVecManager() override;

  // Initialize the embedding model (calls libembedding to load Nomic Embed)
  // Returns true on success. idempotent — safe to call multiple times.
  bool initialize(const QString &cacheDir = QString());

  // Create the vec0 virtual table "chunks" in the given database
  bool createChunksTable(SqliteDb &db);

  // Embed a batch of text strings; returns flat float vectors (rows × dims)
  QVector<float> embed(const QStringList &texts);

  // Insert chunks into the vec0 table. Returns count of inserted rows.
  int insertChunks(SqliteDb &db, const QList<TextChunk> &chunks);

  // Search chunks by query text; returns top-k nearest neighbors
  QList<ChunkSearchResult> search(SqliteDb &db, const QString &query,
                                  int k = 10);

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
  void *m_embeddingCtx = nullptr; // opaque lembed_text_embedding_t*
};
