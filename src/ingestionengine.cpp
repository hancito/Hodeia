#include "ingestionengine.h"
#include "sqlitedb.h"
#include "sqlitevec.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QRegularExpression>
#include <QtConcurrent>

extern "C" {
#include <mupdf/fitz.h>
}
#include <tesseract/baseapi.h>

// ============================================================================
//  Layout-aware text extraction
//
//  MuPDF builds a per-page hierarchy:
//    fz_stext_page
//      └─ fz_stext_block  (TEXT | IMAGE | STRUCT | VECTOR | GRID)
//           └─ fz_stext_line  (shared baseline)
//                └─ fz_stext_char  (Unicode codepoint + position)
//
//  When FZ_STEXT_SEGMENT is enabled (and fz_segment_stext_page() is called),
//  MuPDF splits the flat block list into STRUCT nodes that represent
//  distinct page regions (e.g. separate columns).  A depth-first traversal
//  then visits blocks in the correct reading order.
// ============================================================================

/// Walk one TEXT block's lines → chars and produce a QString.
static QString blockToString(fz_stext_block *block) {
  QString result;
  for (fz_stext_line *line = block->u.t.first_line; line; line = line->next) {
    if (line != block->u.t.first_line)
      result += '\n';
    for (fz_stext_char *ch = line->first_char; ch; ch = ch->next) {
      uint32_t c = ch->c;
      if (c < 0x20 && !QChar(c).isSpace())
        continue; // discard non-whitespace control characters
      // QChar only holds 16-bit codepoints (BMP).  Supplementary characters
      // (U+10000 and above) must be stored as UTF-16 surrogate pairs.
      if (c < 0x10000) {
        result += QChar(c);
      } else {
        // Encode as high + low surrogate pair.
        c -= 0x10000;
        result += QChar(0xD800 + (c >> 10));
        result += QChar(0xDC00 + (c & 0x3FF));
      }
    }
  }
  return result;
}

QString PdfWorker::extractTextFromStextPage(fz_stext_page *textPage) const {
  QString pageText;
  bool firstBlock = true;

  // DFS iterator respects segmentation STRUCT boundaries (column order).
  fz_stext_page_block_iterator it =
      fz_stext_page_block_iterator_begin_dfs(textPage);

  while (!fz_stext_page_block_iterator_eod_dfs(it)) {
    fz_stext_block *block = it.block;
    if (block && block->type == FZ_STEXT_BLOCK_TEXT) {
      QString blockText = blockToString(block);
      blockText.remove(QChar(0x00AD)); // soft hyphens from line wrapping
      QRegularExpression ws(R"(\s+)");
      blockText.replace(ws, QString(" "));
      blockText = blockText.trimmed();

      if (!blockText.isEmpty()) {
        if (!firstBlock)
          pageText += "\n\n";
        pageText += blockText;
        firstBlock = false;
      }
    }
    it = fz_stext_page_block_iterator_next_dfs(it);
  }

  return pageText;
}

// ============================================================================
//  OCR fallback  (Tesseract via direct API — not through MuPDF)
//
//  Each QtConcurrent worker thread keeps one TessBaseAPI (cached per
//  thread in a hash protected by a mutex).  The pool is bounded to the
//  CPU core count, so memory usage is bounded.
//  api->Clear() frees per-page results; the model stays cached for reuse.
//
//  After each batch, cleanupOcrApi() destroys every cached API sequentially
//  from the caller thread.  Because all pool tasks have finished
//  (blockingMap returned), no thread accesses its API concurrently.
// ============================================================================

static QMutex g_ocrMutex;
static QHash<QThread *, tesseract::TessBaseAPI *> g_ocrCache;

static tesseract::TessBaseAPI *ensureOcrApi() {
  QMutexLocker locker(&g_ocrMutex);
  QThread *current = QThread::currentThread();
  tesseract::TessBaseAPI *api = g_ocrCache.value(current, nullptr);
  if (!api) {
    api = new tesseract::TessBaseAPI();

    QString tessdata =
        QDir(QCoreApplication::applicationDirPath()).filePath("tessdata");

    int rc = api->Init(qPrintable(tessdata), "eng+fra",
                       tesseract::OcrEngineMode::OEM_LSTM_ONLY);
    if (rc != 0) {
      qCritical() << "[OCR] Tesseract Init failed (rc=" << rc << ")"
                  << "tessdata:" << tessdata;
      delete api;
      return nullptr;
    }
    api->SetPageSegMode(tesseract::PageSegMode::PSM_AUTO);
    g_ocrCache.insert(current, api);
  }
  return api;
}

/// Destroy all TessBaseAPI instances sequentially from the caller's thread.
///  Safe to call after blockingMap/Mapped returns (all pool workers idle).
static void cleanupOcrApi() {
  QMutexLocker locker(&g_ocrMutex);
  for (auto it = g_ocrCache.begin(); it != g_ocrCache.end(); ++it) {
    tesseract::TessBaseAPI *api = it.value();
    if (api) {
      api->End();
      delete api;
    }
  }
  g_ocrCache.clear();
  qDebug() << "[OCR] Cleanup complete";
}

QString PdfWorker::extractTextWithOcr(fz_context *ctx, fz_document *doc,
                                      int pageNum) const {
  tesseract::TessBaseAPI *api = ensureOcrApi();
  if (!api)
    return QString();

  // Render at 300 DPI grayscale — optimal quality/speed trade-off.
  fz_matrix ctm = fz_scale(300.0f / 72.0f, 300.0f / 72.0f);
  fz_pixmap *pix = fz_new_pixmap_from_page_number(ctx, doc, pageNum, ctm,
                                                  fz_device_gray(ctx), 0);
  if (!pix)
    return QString();

  // Tesseract accepts raw grayscale bytes directly.
  api->SetImage(pix->samples, pix->w, pix->h, 1, pix->stride);
  api->SetSourceResolution(300);

  char *text = api->GetUTF8Text();
  QString result;
  if (text) {
    QByteArray utf8(text);
    delete[] text;
    result = QString::fromUtf8(utf8).trimmed();
    result.remove(
        QRegularExpression("[\\x00-\\x08\\x0B\\x0C\\x0E-\\x1F\\x7F]"));
  }

  api->Clear(); // free per-page results, keep model cached for reuse
  fz_drop_pixmap(ctx, pix);
  return result;
}

// ============================================================================
//  Chunking
// ============================================================================

QList<TextChunk> PdfWorker::chunkDocumentText(
    const QString &fullText, const QVector<quint16> &pageMap,
    const QString &paperHash, const QVector<PageMetadata> &pageMeta) const {
  QList<TextChunk> chunks;

  // Rejoin words broken by OCR line-wraps (e.g. "punish-\nment" →
  // "punishment").
  QString text = fullText;
  text.replace(QRegularExpression("-\\s*\\n\\s*"), "");
  text.replace(QRegularExpression("(?<=[a-zA-Z])\\n(?=[a-zA-Z])"), " ");

  // 1) Sentence segmentation — split after . ! ? followed by whitespace.
  QStringList sentences;
  int lastEnd = 0;
  QRegularExpression sentRe(R"((?<=[.!?])\s+)");
  auto matchIter = sentRe.globalMatch(text);
  while (matchIter.hasNext()) {
    auto match = matchIter.next();
    QString sentence = text.mid(lastEnd, match.capturedStart() - lastEnd +
                                             match.capturedLength());
    if (!sentence.trimmed().isEmpty())
      sentences.append(sentence.trimmed());
    lastEnd = match.capturedEnd();
  }
  if (lastEnd < text.length()) {
    QString remainder = text.mid(lastEnd).trimmed();
    if (!remainder.isEmpty())
      sentences.append(remainder);
  }

  // 2) Greedy sentence packing with fixed overlap.
  //    runningOffset tracks the position in fullText so we can resolve
  //    the page number for each emitted chunk via pageMap.
  QString currentChunk;
  int chunkIdx = 0;
  int runningOffset = 0;

  for (const auto &sent : sentences) {
    QString candidate =
        currentChunk.isEmpty() ? sent : currentChunk + " " + sent;
    if (candidate.length() > m_maxChunkChars && !currentChunk.isEmpty()) {
      TextChunk tc;
      tc.text = currentChunk.trimmed();
      tc.paperHash = paperHash;
      tc.page = pageMap.value(runningOffset, 0);
      tc.chunkIdx = chunkIdx++;
      chunks.append(tc);

      // Keep the tail of the current chunk as overlap for the next one.
      int overlapStart = qMax(0, currentChunk.length() - m_overlapChars);
      QString overlapText = currentChunk.mid(overlapStart);
      runningOffset += overlapText.length();
      currentChunk = overlapText + " " + sent;
    } else {
      currentChunk = candidate;
    }
  }

  // Flush the last accumulated chunk.
  if (!currentChunk.trimmed().isEmpty()) {
    TextChunk tc;
    tc.text = currentChunk.trimmed();
    tc.paperHash = paperHash;
    tc.page = pageMap.value(runningOffset, 0);
    tc.chunkIdx = chunkIdx++;
    chunks.append(tc);
  }

  // Debug: total text in chunks vs input
  int totalChunkChars = 0;
  for (const auto &c : chunks)
    totalChunkChars += c.text.length();

  // Attach page metadata JSON to each chunk.
  if (!pageMeta.isEmpty()) {
    for (auto &chunk : chunks) {
      // pageMap stores 0-based page indices; PageMetadata.page is 1-based.
      int page0 = chunk.page; // 0-based from pageMap
      if (page0 >= 0 && page0 < pageMeta.size()) {
        const PageMetadata &meta = pageMeta[page0];

        // Build JSON metadata string.
        QJsonObject obj;
        obj["page"] = meta.page;
        obj["figures"] = meta.figureCount;
        obj["has_figure"] = meta.hasFigure;
        obj["has_vector_graphics"] = meta.hasVectorGraphics;
        obj["has_table"] = meta.hasTable;
        obj["scanned"] = meta.isScanned;
        if (meta.isScanned)
          obj["ocr_chars"] = meta.ocrChars;

        chunk.pageMetadata = QJsonDocument(obj).toJson(QJsonDocument::Compact);
      }
    }
  }
  qDebug() << "[PdfWorker] chunkDocumentText:" << paperHash.left(12)
           << "| input:" << fullText.length() << "chars"
           << "| output:" << chunks.size() << "chunks," << totalChunkChars
           << "chars total"
           << "| avg chunk:"
           << (chunks.isEmpty() ? 0 : totalChunkChars / chunks.size())
           << "chars"
           << "| first chunk:" << chunks.first().text.left(80) << "...";

  return chunks;
}

// ============================================================================
//  File identity
// ============================================================================

QString PdfWorker::computeFileHash(const QUrl &fileUrl) const {
  QString localPath = fileUrl.toLocalFile();
  if (localPath.isEmpty())
    return QString();
  QFile file(localPath);
  if (!file.open(QIODevice::ReadOnly))
    return QString();
  QCryptographicHash hasher(QCryptographicHash::Md5);
  if (hasher.addData(&file))
    return QString(hasher.result().toHex());
  return QString();
}

// ============================================================================
//  Lifecycle
// ============================================================================

PdfWorker::PdfWorker(ConfigManager *config, SqliteVecManager *vecManager,
                     QObject *parent)
    : QObject(parent), m_vecManager(vecManager), m_config(config) {
  if (m_vecManager) {
    connect(m_vecManager, &SqliteVecManager::embeddingProgress, this,
            &PdfWorker::embeddingProgress);
  }
}

PdfWorker::~PdfWorker() = default;

void PdfWorker::stop() { m_stopFlag.store(true); }

// ============================================================================
//  Main ingestion pipeline
// ============================================================================

void PdfWorker::processPDF(const QList<QUrl> &filePath, const QString &imgPath,
                           const QString &dbPath) {
  m_stopFlag.store(false);
  emit started();

  QString message =
      "Started with " + QString::number(filePath.size()) + " files";
  emit status(message);

  QMap<QString, QUrl> hashesToProcess = parallelHasher(filePath);
  QList<QUrl> urlsToProcess = filterExistingHashes(hashesToProcess, dbPath);
  message = "Skipped " +
            QString::number(filePath.size() - urlsToProcess.size()) + " papers";
  emit status(message);

  // Process in small batches to bound memory and provide incremental progress.
  const int batchSize = 10;
  int savedCount = 0;
  int errorCount = 0;
  int total = urlsToProcess.size();

  for (int i = 0; i < total; i += batchSize) {
    if (m_stopFlag.load()) {
      emit status("Ingestion cancelled");
      break;
    }
    int end = qMin(i + batchSize, total);
    QList<QUrl> batchUrls(urlsToProcess.mid(i, end - i));

    message = QString("Extracting %1-%2/%3").arg(i + 1).arg(end).arg(total);
    emit status(message);

    QList<ExtractionResult> extractions = parallelExtract(batchUrls, imgPath);

    QList<SummaryRow> rows;
    QList<TextChunk> allChunks;

    for (auto &extraction : extractions) {
      if (extraction.title.isEmpty()) {
        qCritical() << "[PdfWorker] Skipping file: empty extraction";
        ++errorCount;
        continue;
      }

      SummaryRow row;
      row.paperHash = extraction.paperHash;
      row.title = extraction.title;
      row.pagesNum = extraction.pageCount;
      row.date = QDateTime::currentDateTimeUtc();
      rows.append(row);

      // Propagate title as initial cleanTitle and citation on every chunk.
      // The summarizer will overwrite both with proper authors/year later.
      for (auto &tc : extraction.chunks) {
        tc.cleanTitle = extraction.title;
        tc.citation = extraction.title;
      }

      allChunks << std::move(extraction.chunks);
    }

    if (!rows.isEmpty()) {
      savePapers(rows, dbPath);
      savedCount += rows.size();
    }

    if (!allChunks.isEmpty()) {
      // Debug: chunk distribution per paper
      QMap<QString, int> chunksByPaper;
      int totalChunkChars = 0;
      for (const auto &c : allChunks) {
        chunksByPaper[c.paperHash]++;
        totalChunkChars += c.text.length();
      }
      qDebug() << "[PdfWorker] Before embedding:" << allChunks.size()
               << "chunks," << totalChunkChars << "chars total"
               << "| papers:" << chunksByPaper.size();
      for (auto it = chunksByPaper.begin(); it != chunksByPaper.end(); ++it)
        qDebug() << "  " << it.key().left(12) << "→" << it.value() << "chunks";

      QString statusMsg =
          QString("Embedding %1 chunks...").arg(allChunks.size());
      emit status(statusMsg);

      if (m_vecManager) {
        ingestChunks(allChunks, dbPath);
      } else {
        qWarning() << "[PdfWorker] embedding model not loaded, skipping vector "
                      "storage";
        emit status("Warning: embedding model not loaded, skipping vector "
                    "storage");
      }

      allChunks.clear();
      allChunks.squeeze();
    }

    // Free Tesseract APIs from the thread pool after each batch so that
    // memory is released incrementally rather than held until process exit.
    cleanupOcrApi();

    message =
        QString("Saved %1/%2 papers").arg(savedCount + errorCount).arg(total);
    emit status(message);
  }

  if (errorCount > 0)
    qWarning() << "[PdfWorker] Total skipped due to errors:" << errorCount;

  emit finished();
}

// ============================================================================
//  Database writes
// ============================================================================

void PdfWorker::ingestChunks(const QList<TextChunk> &chunks,
                             const QString &dbPath) {
  if (chunks.isEmpty())
    return;

  SqliteDb db;
  auto openResult = db.open(dbPath);
  if (!openResult) {
    qCritical() << "[PdfWorker] Failed to open database for chunk ingestion:"
                << openResult.error();
    return;
  }

  auto txResult = db.transaction();
  if (!txResult) {
    qCritical() << "[PdfWorker] Failed to start transaction for chunks:"
                << txResult.error();
    return;
  }

  int inserted = m_vecManager->insertChunks(db, chunks);

  qDebug() << "[PdfWorker] insertChunks returned:" << inserted << "out of"
           << chunks.size() << "chunks";

  if (inserted < 0) {
    auto rbResult = db.rollback();
    if (!rbResult) {
      qCritical() << "[PdfWorker] Chunk transaction rollback failed:"
                  << rbResult.error();
    }
    return;
  }

  auto commitResult = db.commit();
  if (!commitResult) {
    qCritical() << "[PdfWorker] Chunk transaction commit failed:"
                << commitResult.error();
    (void)db.rollback();
  }

  auto wpResult = db.exec("PRAGMA wal_checkpoint(TRUNCATE)");
  if (!wpResult) {
    qCritical() << "[PdfWorker] WAL checkpoint failed:" << wpResult.error();
  }
}

void PdfWorker::savePapers(const QList<SummaryRow> &rows,
                           const QString &dbPath) {
  if (rows.isEmpty())
    return;

  SqliteDb db;
  auto openResult = db.open(dbPath);
  if (!openResult) {
    qCritical() << "[PdfWorker] Failed to open database for saving in thread"
                << QThread::currentThreadId() << ":" << openResult.error();
    return;
  }

  auto txResult = db.transaction();
  if (!txResult) {
    qCritical() << "[PdfWorker] Failed to start transaction:"
                << txResult.error();
    return;
  }

  SqliteStatement stmt =
      db.prepare("INSERT INTO Summaries (paper_hash, title, "
                 "authors, "
                 "pages_num, journal, year, topics, llmodel, "
                 "summary_json, date) "
                 "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");

  for (const auto &row : rows) {
    auto b1 = stmt.bind(1, row.paperHash);
    auto b2 = stmt.bind(2, row.title);
    auto b3 = stmt.bind(3, row.authors);
    auto b4 = stmt.bind(4, row.pagesNum);
    auto b5 = stmt.bind(5, row.journal);
    auto b6 = stmt.bind(6, row.year);
    auto b7 = stmt.bind(7, row.topics);
    auto b8 = stmt.bind(8, row.llmodel);
    auto b9 = stmt.bind(9, row.summaryJson.toByteArray());
    auto b10 = stmt.bind(10, row.date);

    if (!b1 || !b2 || !b3 || !b4 || !b5 || !b6 || !b7 || !b8 || !b9 || !b10) {
      QString errMsg;
      if (!b1)
        errMsg = b1.error();
      else if (!b2)
        errMsg = b2.error();
      else if (!b3)
        errMsg = b3.error();
      else if (!b4)
        errMsg = b4.error();
      else if (!b5)
        errMsg = b5.error();
      else if (!b6)
        errMsg = b6.error();
      else if (!b7)
        errMsg = b7.error();
      else if (!b8)
        errMsg = b8.error();
      else if (!b9)
        errMsg = b9.error();
      else
        errMsg = b10.error();
      qCritical() << "[PdfWorker] Error binding params for paper" << row.title
                  << ":" << errMsg;
      (void)db.rollback();
      return;
    }

    auto execResult = stmt.exec();
    if (!execResult) {
      qCritical() << "[PdfWorker] Error inserting paper" << row.title << ":"
                  << execResult.error();
      (void)db.rollback();
      return;
    }
    stmt.resetAndClearBindings();
  }

  auto commitResult = db.commit();
  if (!commitResult) {
    qCritical() << "[PdfWorker] Transaction commit failed:"
                << commitResult.error();
    (void)db.rollback();
  }

  auto wpResult = db.exec("PRAGMA wal_checkpoint(TRUNCATE)");
  if (!wpResult) {
    qCritical() << "[PdfWorker] WAL checkpoint failed:" << wpResult.error();
  }
}

// ============================================================================
//  Hash helpers
// ============================================================================

QStringList PdfWorker::getAllPaperHashes(const QString &dbPath) const {
  QStringList hashes;

  SqliteDb db;
  auto openResult = db.open(dbPath);
  if (!openResult) {
    qCritical() << "[PdfWorker] Failed to open database for reading:"
                << openResult.error();
    return hashes;
  }

  SqliteStatement stmt = db.prepare("SELECT paper_hash FROM Summaries");
  while (true) {
    auto stepResult = stmt.step();
    if (!stepResult) {
      qCritical() << "[PdfWorker] Error reading paper hashes:"
                  << stepResult.error();
      break;
    }
    if (!stepResult.value())
      break;
    hashes << stmt.columnText(0);
  }
  return hashes;
}

QMap<QString, QUrl>
PdfWorker::parallelHasher(const QList<QUrl> &fileList) const {
  QMap<QString, QUrl> hashToUrl;

  QList<QPair<QString, QUrl>> hashUrlPairs =
      QtConcurrent::blockingMapped(fileList, [this](const QUrl &url) {
        return qMakePair(computeFileHash(url), url);
      });

  for (const auto &pair : hashUrlPairs) {
    hashToUrl.insert(pair.first, pair.second);
  }

  return hashToUrl;
}

QList<QUrl>
PdfWorker::filterExistingHashes(const QMap<QString, QUrl> &hashesToProcess,
                                const QString &dbPath) const {
  QStringList existingHashes = getAllPaperHashes(dbPath);
  QList<QUrl> urlsToProcess;

  for (auto it = hashesToProcess.constBegin(); it != hashesToProcess.constEnd();
       ++it) {
    if (!existingHashes.contains(it.key())) {
      urlsToProcess.append(it.value());
    }
  }

  return urlsToProcess;
}

// ============================================================================
//  PDF extraction  (MuPDF + Tesseract fallback)
//
//  Per-page pipeline:
//    1. Load page, build structured-text with segmentation options.
//    2. Run fz_segment_stext_page + fz_paragraph_break to reorder columns.
//    3. Walk the block tree (DFS) to extract text in reading order.
//    4. If the page yields < 100 chars, re-render at 300 DPI grayscale
//       and run Tesseract OCR as a fallback.
//    5. Render a 150 DPI thumbnail for the UI.
//    6. Accumulate full-text and a pageMap for cross-page chunking.
// ============================================================================

ExtractionResult PdfWorker::extractText(const QUrl &fileUrl,
                                        const QString &imgPath) const {
  QString localPath = fileUrl.toLocalFile();
  ExtractionResult result;

  if (localPath.isEmpty()) {
    result.title = "Error: Invalid path.";
    return result;
  }

  fz_context *ctx = nullptr;
  fz_document *doc = nullptr;

  ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
  if (!ctx) {
    result.title = "Error: Cannot register MuPDF context.";
    return result;
  }

  fz_try(ctx) { fz_register_document_handlers(ctx); }
  fz_catch(ctx) {
    fz_drop_context(ctx);
    result.title = "Error: Failed to register document handlers.";
    return result;
  }

  fz_try(ctx) { doc = fz_open_document(ctx, localPath.toUtf8().constData()); }
  fz_catch(ctx) {
    fz_drop_context(ctx);
    result.title = "Error: Cannot open document " + localPath;
    return result;
  }

  int pageCount = 0;
  float zoom = 150.0f / 72.0f; // thumbnail resolution
  fz_matrix ctm = fz_scale(zoom, zoom);

  fz_try(ctx) { pageCount = fz_count_pages(ctx, doc); }
  fz_catch(ctx) {
    fz_drop_document(ctx, doc);
    fz_drop_context(ctx);
    result.title = "Error: Cannot count pages.";
    return result;
  }

  result.pageCount = pageCount;
  result.paperHash = computeFileHash(fileUrl);
  result.title = QFileInfo(localPath).baseName();
  result.pageMetadata.reserve(pageCount);

  QString fullText;
  QVector<quint16> pageMap; // char offset → page number (compact: 2 bytes)

  for (int i = 0; i < pageCount; ++i) {
    fz_page *page = nullptr;
    fz_stext_page *textPage = nullptr;
    fz_device *dev = nullptr;
    fz_pixmap *pix = nullptr;

    // Per-page metadata
    PageMetadata pageMeta;
    pageMeta.page = i + 1;

    fz_try(ctx) {
      // ------------------------------------------------------------------
      // 1. Build structured text with layout-aware options + image detection
      // ------------------------------------------------------------------
      page = fz_load_page(ctx, doc, i);
      if (!page) {
        qWarning() << "[PdfWorker] fz_load_page failed for page" << i + 1
                   << "— skipping";
        break;
      }
      textPage = fz_new_stext_page(ctx, fz_bound_page(ctx, page));

      fz_stext_options opts = {};
      opts.flags = FZ_STEXT_SEGMENT | FZ_STEXT_PARAGRAPH_BREAK |
                   FZ_STEXT_DEHYPHENATE | FZ_STEXT_CLIP |
                   FZ_STEXT_PRESERVE_SPANS | FZ_STEXT_PRESERVE_IMAGES |
                   FZ_STEXT_COLLECT_VECTORS;
      opts.scale = 1.0f;
      opts.clip = fz_empty_rect;

      dev = fz_new_stext_device(ctx, textPage, &opts);
      fz_run_page(ctx, page, dev, fz_identity, nullptr);
      fz_close_device(ctx, dev);
      dev = nullptr;

      fz_segment_stext_page(ctx, textPage);
      fz_paragraph_break(ctx, textPage);

      // Hunt for tables → creates GRID blocks.
      fz_table_hunt(ctx, textPage);

      // ------------------------------------------------------------------
      // 1b. Detect figures, vector graphics, and tables
      // ------------------------------------------------------------------
      {
        fz_stext_page_block_iterator it =
            fz_stext_page_block_iterator_begin_dfs(textPage);
        while (!fz_stext_page_block_iterator_eod_dfs(it)) {
          fz_stext_block *block = it.block;
          if (block && block->type == FZ_STEXT_BLOCK_IMAGE) {
            pageMeta.figureCount++;
            pageMeta.hasFigure = true;
          }
          if (block && block->type == FZ_STEXT_BLOCK_VECTOR) {
            // Vector blocks: lines, curves, rects drawn as paths.
            // These represent charts, graphs, diagrams, decorative elements.
            // Filter out tiny vectors (underlines, diacritics, rules) by bbox.
            fz_rect vb = block->bbox;
            float area = (vb.x1 - vb.x0) * (vb.y1 - vb.y0);
            if (area > 500) { // > ~22x22 pts — meaningful graphic
              pageMeta.hasVectorGraphics = true;
              // Count vector graphics as figures too.
              pageMeta.figureCount++;
              pageMeta.hasFigure = true;
            }
          }
          if (block && block->type == FZ_STEXT_BLOCK_GRID) {
            // fz_table_hunt creates GRID blocks for any aligned text structure.
            // This includes real tables but also justified text that looks
            // column-aligned. The caller (LLM) decides if it's meaningful.
            fz_stext_grid_info *grid = block->u.b.info;
            if (grid && grid->w >= 2 && grid->h >= 2) {
              int contentCells = 0;
              for (int g = 0; g < grid->w * grid->h; ++g) {
                if (grid->info[g].flags & FZ_STEXT_GRID_FULL)
                  contentCells++;
              }
              if (contentCells >= 4) {
                pageMeta.hasTable = true;
              }
            }
          }
          it = fz_stext_page_block_iterator_next_dfs(it);
        }
      }

      // ------------------------------------------------------------------
      // 2. Thumbnail (150 DPI RGB)
      // ------------------------------------------------------------------
      pix = fz_new_pixmap_from_page_number(ctx, doc, i, ctm, fz_device_rgb(ctx),
                                           0);
      QImage img(pix->samples, pix->w, pix->h, pix->stride,
                 QImage::Format_RGB888);
      img = img.copy();

      QString fileName = QString("%1_page_%2.png")
                             .arg(result.paperHash)
                             .arg(i + 1, 3, 10, QChar('0'));
      if (!img.save(QDir(imgPath).filePath(fileName), "PNG")) {
        qCritical() << "[PdfWorker] Failed to save thumbnail:" << i + 1;
      }
      fz_drop_pixmap(ctx, pix);
      pix = nullptr;

      // ------------------------------------------------------------------
      // 3. Extract text (layout-aware), with OCR fallback
      // ------------------------------------------------------------------
      QString pageTextStr = extractTextFromStextPage(textPage);

      if (pageTextStr.trimmed().length() < 100) {
        pageMeta.isScanned = true;
        qDebug() << QString("[OCR] page %1/%2: %3 chars, falling back to OCR")
                        .arg(i + 1)
                        .arg(pageCount)
                        .arg(pageTextStr.trimmed().length());
        pageTextStr = extractTextWithOcr(ctx, doc, i);
        pageMeta.ocrChars = pageTextStr.length();
        qDebug() << QString("[OCR] page %1/%2: %4 chars produced")
                        .arg(i + 1)
                        .arg(pageCount)
                        .arg(pageTextStr.length());
      }

      // NFKC normalization expands ligatures (ﬁ → fi).
      pageTextStr = pageTextStr.normalized(QString::NormalizationForm_KC);

      // ------------------------------------------------------------------
      // 4. Accumulate
      // ------------------------------------------------------------------
      if (i > 0) {
        fullText.append('\n');
        pageMap.push_back(i); // page-boundary marker
      }
      for (int j = 0; j < pageTextStr.length(); ++j)
        pageMap.push_back(i);
      fullText.append(pageTextStr);
    }
    fz_always(ctx) {
      if (pix)
        fz_drop_pixmap(ctx, pix);
      if (dev)
        fz_drop_device(ctx, dev);
      if (textPage)
        fz_drop_stext_page(ctx, textPage);
      if (page)
        fz_drop_page(ctx, page);
    }
    fz_catch(ctx) { qWarning() << "[PdfWorker] Error extracting page" << i; }

    // Store page metadata (outside fz_try/fz_catch so it always runs)
    result.pageMetadata.append(pageMeta);
  }

  result.chunks = chunkDocumentText(fullText, pageMap, result.paperHash,
                                    result.pageMetadata);

  qDebug() << "[PdfWorker] extractText done:" << result.paperHash.left(12)
           << "| pages:" << result.pageCount
           << "| fullText:" << fullText.length() << "chars"
           << "| chunks:" << result.chunks.size();

  // Log page metadata summary
  int totalFigures = 0, totalPages = 0, scannedPages = 0;
  for (const auto &m : result.pageMetadata) {
    if (m.hasFigure)
      totalFigures += m.figureCount;
    if (m.hasTable)
      totalPages++;
    if (m.isScanned)
      scannedPages++;
  }
  qDebug() << "[PdfWorker] metadata:" << totalFigures << "figures,"
           << totalPages << "pages with tables," << scannedPages
           << "scanned pages | across" << result.pageMetadata.size()
           << "pages total";

  fz_drop_document(ctx, doc);
  fz_drop_context(ctx);

  return result;
}

// ============================================================================
//  Parallel dispatch
// ============================================================================

QList<ExtractionResult>
PdfWorker::parallelExtract(const QList<QUrl> &filePaths,
                           const QString &imgPath) const {
  QList<ExtractionResult> results;
  for (const auto &url : filePaths) {
    if (m_stopFlag.load())
      break;
    results.append(extractText(url, imgPath));
  }
  return results;
}
