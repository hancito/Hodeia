#pragma once

#include "ingestionpipeline.h"
#include "summary.h"

#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QUrl>
#include <QVector>
#include <atomic>
#include <functional>

class SqliteVecManager;

/// Text of a PDF for the library: MuPDF's layout-aware text, Tesseract OCR
/// for the pages without text, page images for the PDF tab, and chunks
/// sized for the embedding model.  No database, no Qt object: extract() is
/// safe to call from several threads at once (one file each).
class PdfExtractor {
  Q_DECLARE_TR_FUNCTIONS(PdfExtractor)

public:
  /// Called before each page of a file is read: its name, the page
  /// (1-based) and the page count.  From the extracting thread.
  using PageProgressFn =
      std::function<void(const QString &fileName, int page, int pageCount)>;

  /// `vecManager` gives the chunk size and token counts (null: estimates);
  /// once `stop` is set (if given), extract() ends at the next page.
  explicit PdfExtractor(SqliteVecManager *vecManager,
                        const std::atomic<bool> *stop = nullptr);

  /// Text, page metadata and chunks of one file; its page images are saved
  /// to `imgPath` as <hash>_page_NNN.png.  `error` is set when the file
  /// cannot be read.
  ExtractionResult extract(const IngestionJob &job, const QString &imgPath,
                           const PageProgressFn &progress = {}) const;

  /// MD5 of a file, hex: the paper's identity in the library.  Empty if the
  /// file cannot be read.
  static QString fileHash(const QUrl &fileUrl);

private:
  QList<TextChunk>
  chunkDocumentText(const QString &fullText, const QVector<quint16> &pageMap,
                    const QString &paperHash,
                    const QVector<PageMetadata> &pageMeta) const;

  // Chunk size limit (tokens) when the embedding model can't be loaded to
  // provide its own; otherwise chunks follow SqliteVecManager::maxChunkTokens.
  static constexpr int FallbackMaxChunkTokens = 480;

  SqliteVecManager *m_vecManager = nullptr;
  const std::atomic<bool> *m_stop = nullptr;
};
