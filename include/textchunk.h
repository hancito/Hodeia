#pragma once

#include <QString>

/// A piece of a paper's text to embed and store in the chunks table
/// (produced by the ingestion, stored by LibraryRepository::addChunks).
struct TextChunk {
  QString text;
  QString paperHash;
  int page = 0;
  int chunkIdx = 0;
  QString cleanTitle;
  QString citation;
  QString pageMetadata; ///< JSON metadata: figures, tables, scanned, etc.
};
