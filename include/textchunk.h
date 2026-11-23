#pragma once

#include <QString>

/// A piece of a paper's text to embed and store in the chunks table
/// (produced by the ingestion, stored by LibraryRepository::addChunks).
/// A stored chunk read back from the chunks table: by a similarity search
/// (LibraryRepository::nearestChunks) or for a whole paper
/// (LibraryRepository::chunks, distance 0).
struct ChunkSearchResult {
  QString paperHash;
  int page = 0; ///< 0-based
  int chunkIdx = 0;
  QString text;
  float distance = 0.0f;
  QString citation;
  QString cleanTitle;
  QString pageMetadata; ///< JSON: figures, tables, scanned flags
};

struct TextChunk {
  QString text;
  QString paperHash;
  int page = 0;
  int chunkIdx = 0;
  QString cleanTitle;
  QString citation;
  QString pageMetadata; ///< JSON metadata: figures, tables, scanned, etc.
};
