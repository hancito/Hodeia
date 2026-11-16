#include "ingestionengine.h"
#include "notifier.h"
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
      Notifier::post(
          "ocr-init", PdfWorker::tr("Text Recognition Unavailable"),
          PdfWorker::tr("The OCR engine could not start, so scanned pages "
                        "(images without text) cannot be read. Papers with "
                        "selectable text are not affected.\n\nThe language "
                        "data may be missing from:\n%1\n\nReinstalling "
                        "Hodeia should fix it.")
              .arg(QDir::toNativeSeparators(tessdata)));
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

  // 0) Normalise the text while keeping a per-character page map aligned
  //    with it: every character removed from the text is removed from the
  //    page map too, so offsets in `text` always resolve to the right page.
  //
  //    Rejoin words broken by OCR line-wraps ("punish-\nment" →
  //    "punishment"); this shortens the text, hence the joint rebuild.
  static const QRegularExpression hyphenBreak(QStringLiteral("-\\s*\\n\\s*"));
  const quint16 lastPage = pageMap.isEmpty() ? 0 : pageMap.last();
  QString text;
  QVector<quint16> textPages;
  text.reserve(fullText.size());
  textPages.reserve(fullText.size());
  auto copyRange = [&](qsizetype from, qsizetype to) {
    text += QStringView(fullText).mid(from, to - from);
    for (qsizetype i = from; i < to; ++i)
      textPages.push_back(i < pageMap.size() ? pageMap[i] : lastPage);
  };
  {
    qsizetype pos = 0;
    auto it = hyphenBreak.globalMatch(fullText);
    while (it.hasNext()) {
      const auto m = it.next();
      copyRange(pos, m.capturedStart());
      pos = m.capturedEnd();
    }
    copyRange(pos, fullText.size());
  }
  // Same-length substitution (one '\n' → one ' '): the page map stays valid.
  static const QRegularExpression softWrap(
      QStringLiteral("(?<=[a-zA-Z])\\n(?=[a-zA-Z])"));
  text.replace(softWrap, QStringLiteral(" "));

  auto pageAt = [&](qsizetype offset) -> int {
    return offset < textPages.size() ? textPages[offset] : lastPage;
  };

  // 1) Sentence segmentation — split after . ! ? followed by whitespace.
  //    Each sentence keeps the offset of its first non-space character so
  //    its page can be resolved exactly.
  struct Sentence {
    QString text;
    int page;
  };
  QList<Sentence> sentences;
  auto addSentence = [&](qsizetype from, qsizetype to) {
    while (from < to && text[from].isSpace())
      ++from;
    const QString s = text.mid(from, to - from).trimmed();
    if (!s.isEmpty())
      sentences.append({s, pageAt(from)});
  };
  {
    static const QRegularExpression sentRe(QStringLiteral("(?<=[.!?])\\s+"));
    qsizetype lastEnd = 0;
    auto it = sentRe.globalMatch(text);
    while (it.hasNext()) {
      const auto m = it.next();
      addSentence(lastEnd, m.capturedEnd());
      lastEnd = m.capturedEnd();
    }
    addSentence(lastEnd, text.size());
  }

  // 2) Token budget, taken from the embedding model so no chunk is ever
  //    truncated when it is embedded (the tail of an over-long chunk would
  //    be unsearchable).  Chunks aim at ~3/4 of the model's limit and carry
  //    whole sentences of overlap from the previous chunk.
  const int hardMax = [&] {
    const int fromModel = m_vecManager ? m_vecManager->maxChunkTokens() : 0;
    return fromModel > 0 ? fromModel : FallbackMaxChunkTokens;
  }();
  const int target = hardMax * 3 / 4;
  const int overlapBudget = target / 6;

  auto countTokens = [&](const QString &t) -> int {
    const int n = m_vecManager ? m_vecManager->countTokens(t) : -1;
    // Fallback estimate: scientific text averages well under 3 chars/token.
    return n >= 0 ? n : int(t.length() / 3) + 1;
  };

  // A sentence longer than the limit (tables, reference lists, OCR without
  // punctuation) is cut at whitespace into pieces of about `target` tokens.
  struct Piece {
    QString text;
    int page;
    int tokens;
  };
  QList<Piece> pieces;
  for (const auto &sent : sentences) {
    const int n = countTokens(sent.text);
    if (n <= hardMax) {
      pieces.append({sent.text, sent.page, n});
      continue;
    }
    const double charsPerToken = double(sent.text.length()) / n;
    const int pieceChars = qMax(32, int(target * charsPerToken));
    qsizetype from = 0;
    while (from < sent.text.length()) {
      qsizetype to = qMin(sent.text.length(), from + pieceChars);
      if (to < sent.text.length()) {
        const qsizetype space = sent.text.lastIndexOf(' ', to);
        if (space > from)
          to = space;
      }
      const QString part = sent.text.mid(from, to - from).trimmed();
      if (!part.isEmpty())
        pieces.append({part, sent.page, countTokens(part)});
      from = to;
    }
  }

  // 3) Greedy packing.  A chunk's page is the page of its first *new* piece
  //    (not of the overlap carried over from the previous chunk): that is
  //    where most of its content comes from.
  QList<Piece> current;
  int currentTokens = 0;
  int firstNew = 0; // index in `current` of the first non-overlap piece
  int chunkIdx = 0;
  int maxEmitted = 0;

  auto emitChunk = [&]() {
    QStringList parts;
    for (const auto &pc : current)
      parts << pc.text;
    TextChunk tc;
    tc.text = parts.join(' ');
    tc.paperHash = paperHash;
    tc.page = current[qMin(firstNew, int(current.size()) - 1)].page;
    tc.chunkIdx = chunkIdx++;
    chunks.append(tc);
    maxEmitted = qMax(maxEmitted, currentTokens);
  };

  for (const auto &piece : pieces) {
    if (current.size() > firstNew && currentTokens + piece.tokens > target) {
      emitChunk();
      // Carry trailing whole pieces as overlap (never the whole chunk).
      QList<Piece> keep;
      int keepTokens = 0;
      for (qsizetype i = current.size() - 1; i > 0; --i) {
        if (keepTokens + current[i].tokens > overlapBudget)
          break;
        keep.prepend(current[i]);
        keepTokens += current[i].tokens;
      }
      current = keep;
      currentTokens = keepTokens;
      firstNew = int(current.size());
    }
    // Overlap + a long piece could exceed what the model reads: drop the
    // overlap rather than truncate the chunk.
    if (currentTokens + piece.tokens > hardMax) {
      current.clear();
      currentTokens = 0;
      firstNew = 0;
    }
    current.append(piece);
    currentTokens += piece.tokens;
  }

  // Flush the last accumulated chunk.
  if (current.size() > firstNew)
    emitChunk();

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
           << "| tokens: target" << target << "max" << hardMax
           << "largest" << maxEmitted << "| avg chunk:"
           << (chunks.isEmpty() ? 0 : totalChunkChars / chunks.size())
           << "chars"
           << "| first chunk:"
           << (chunks.isEmpty() ? QString() : chunks.first().text.left(80))
           << "...";

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
    // Emitted from this worker's thread (ingestChunks): a direct call.
    connect(m_vecManager, &SqliteVecManager::embeddingProgress, this,
            [this](int current, int total) {
              emit stepProgress(QStringLiteral("embed"), current, total,
                                m_embedDetail);
            });
  }
}

PdfWorker::~PdfWorker() = default;

void PdfWorker::stop() { m_stopFlag.store(true); }

void PdfWorker::cancel() {
  m_userCancelled.store(true);
  m_stopFlag.store(true);
}

// ============================================================================
//  Main ingestion pipeline
// ============================================================================

void PdfWorker::processPDF(const QList<QUrl> &filePath, const QString &imgPath,
                           const QString &dbPath) {
  m_stopFlag.store(false);
  m_userCancelled.store(false);
  emit started();

  QString message = tr("Started with %1 files").arg(filePath.size());
  emit status(message);

  emit stepProgress(QStringLiteral("hash"), 0, filePath.size(), QString());
  QMap<QString, QUrl> hashesToProcess = parallelHasher(filePath);
  if (m_stopFlag.load()) {
    if (m_userCancelled.load())
      emitCancelReport(0);
    emit finished();
    return;
  }
  emit stepProgress(QStringLiteral("hash"), filePath.size(), filePath.size(),
                    tr("Checking which papers are already in the library..."));
  QList<QUrl> urlsToProcess = filterExistingHashes(hashesToProcess, dbPath);
  const int alreadyKnown = filePath.size() - urlsToProcess.size();
  message = tr("Skipped %1 papers").arg(alreadyKnown);
  emit status(message);
  emit stepProgress(QStringLiteral("hash"), filePath.size(), filePath.size(),
                    tr("%n paper(s) already in the library", "", alreadyKnown));

  // Process in small batches to bound memory and provide incremental progress.
  const int batchSize = 10;
  int savedCount = 0;
  int errorCount = 0;
  QStringList failedTitles; // papers dropped because indexing failed
  QStringList unreadable;   // "file — reason" for files that gave no text
  QString indexError;       // why indexing failed, when known
  bool cancelled = false;
  int total = urlsToProcess.size();
  const int batchCount = (total + batchSize - 1) / batchSize;

  m_extractDone = 0;
  m_extractTotal = total;
  emit papersProgress(0, total);
  emit stepProgress(QStringLiteral("extract"), 0, total, QString());

  for (int i = 0; i < total; i += batchSize) {
    if (m_stopFlag.load()) {
      cancelled = true;
      break;
    }
    int end = qMin(i + batchSize, total);
    QList<QUrl> batchUrls(urlsToProcess.mid(i, end - i));

    message = tr("Extracting %1-%2/%3").arg(i + 1).arg(end).arg(total);
    emit status(message);

    QList<ExtractionResult> extractions = parallelExtract(batchUrls, imgPath);
    // Stopped during the extraction: drop this batch, nothing is saved yet.
    if (m_stopFlag.load()) {
      cleanupOcrApi();
      cancelled = true;
      break;
    }

    QList<SummaryRow> rows;
    QList<TextChunk> allChunks;

    for (auto &extraction : extractions) {
      // A file that could not be opened, or gave no text at all (not even
      // through OCR), can be neither summarized nor queried: don't add it.
      if (!extraction.error.isEmpty() || extraction.paperHash.isEmpty() ||
          extraction.title.isEmpty() || extraction.chunks.isEmpty()) {
        const QString reason = !extraction.error.isEmpty()
                                   ? extraction.error
                                   : tr("no text could be extracted");
        qCritical() << "[PdfWorker] Skipping file" << extraction.fileName
                    << ":" << reason;
        unreadable << extraction.fileName + " — " + reason;
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

      QString statusMsg = tr("Embedding %1 chunks...").arg(allChunks.size());
      emit status(statusMsg);
      m_embedDetail =
          tr("Batch %1/%2").arg(i / batchSize + 1).arg(batchCount);
      emit stepProgress(QStringLiteral("embed"), 0, allChunks.size(),
                        m_embedDetail);

      bool indexed = false;
      if (m_vecManager) {
        indexed = ingestChunks(allChunks, dbPath);
      } else {
        qWarning() << "[PdfWorker] embedding model not loaded, skipping vector "
                      "storage";
      }
      // Stopped during the indexing: the chunks were rolled back, so remove
      // this batch's papers too (a re-import would skip them otherwise).
      if (!indexed && m_stopFlag.load()) {
        removePapers(rows, dbPath);
        savedCount -= rows.size();
        cleanupOcrApi();
        cancelled = true;
        break;
      }
      if (!indexed) {
        if (indexError.isEmpty() && m_vecManager)
          indexError = m_vecManager->lastError();
        // A paper without chunks can be neither summarized nor queried, and
        // a re-import would skip it (its hash is already known): remove it
        // so the user can add it again once the cause is fixed.
        removePapers(rows, dbPath);
        savedCount -= rows.size();
        errorCount += rows.size();
        for (const auto &row : rows)
          failedTitles << row.title;
      }

      allChunks.clear();
      allChunks.squeeze();
    }

    // Free Tesseract APIs from the thread pool after each batch so that
    // memory is released incrementally rather than held until process exit.
    cleanupOcrApi();

    message = tr("Saved %1/%2 papers").arg(savedCount + errorCount).arg(total);
    emit status(message);
    emit papersProgress(savedCount + errorCount, total);
  }

  if (errorCount > 0)
    qWarning() << "[PdfWorker] Total skipped due to errors:" << errorCount;

  // No report when the app is closing (stop()), only for the user's cancel.
  // A cancel that came after the last batch changed nothing: normal report.
  if (cancelled) {
    emit status(tr("Ingestion cancelled"));
    if (m_userCancelled.load())
      emitCancelReport(savedCount);
  } else if (!m_stopFlag.load() || m_userCancelled.load()) {
    emitReport(filePath.size(), alreadyKnown, savedCount, unreadable,
               failedTitles, indexError);
  }

  emit finished();
}

void PdfWorker::emitCancelReport(int added) {
  QString message =
      tr("%n paper(s) were added to the library before the import was "
         "cancelled.",
         nullptr, added);
  message += "\n\n" + tr("To import the remaining files, add them again: "
                          "the papers already in the library will be "
                          "skipped.");
  emit report(tr("Import Cancelled"), message, false);
}

void PdfWorker::emitReport(int selected, int alreadyKnown, int added,
                           const QStringList &unreadable,
                           const QStringList &unindexed,
                           const QString &indexError) {
  if (selected == 0) {
    emit report(tr("Nothing to Import"),
                tr("No PDF file was found in the selection."), false);
    return;
  }

  QStringList parts;
  parts << tr("%n paper(s) added to the library.", nullptr, added);
  if (alreadyKnown > 0)
    parts << tr("%n file(s) skipped: already in the library.", nullptr,
                alreadyKnown);
  if (!unreadable.isEmpty())
    parts << tr("%n file(s) could not be read:", nullptr,
                int(unreadable.size())) +
                 "\n• " + unreadable.join("\n• ");
  if (!unindexed.isEmpty()) {
    QString cause = indexError.isEmpty()
                        ? tr("The embedding model could not process their "
                             "text. See the log for details.")
                        : indexError;
    parts << tr("%n paper(s) could not be indexed for search and were not "
                "added:",
                nullptr, int(unindexed.size())) +
                 "\n• " + unindexed.join("\n• ") + "\n\n" + cause + "\n" +
                 tr("Add the files again once the problem is fixed.");
  }

  const bool failed = !unreadable.isEmpty() || !unindexed.isEmpty();
  QString title = tr("Import Complete");
  if (failed)
    title = added > 0 ? tr("Import Partly Failed") : tr("Import Failed");
  emit report(title, parts.join("\n\n"), failed);
}

// ============================================================================
//  Database writes
// ============================================================================

/// Embed and store the chunks in one transaction.  Returns false when nothing
/// was stored (the transaction is rolled back on any failure).
bool PdfWorker::ingestChunks(const QList<TextChunk> &chunks,
                             const QString &dbPath) {
  if (chunks.isEmpty())
    return true;

  SqliteDb db;
  auto openResult = db.open(dbPath);
  if (!openResult) {
    qCritical() << "[PdfWorker] Failed to open database for chunk ingestion:"
                << openResult.error();
    return false;
  }

  auto txResult = db.transaction();
  if (!txResult) {
    qCritical() << "[PdfWorker] Failed to start transaction for chunks:"
                << txResult.error();
    return false;
  }

  int inserted = m_vecManager->insertChunks(db, chunks, &m_stopFlag);

  qDebug() << "[PdfWorker] insertChunks returned:" << inserted << "out of"
           << chunks.size() << "chunks";

  if (inserted < 0) {
    auto rbResult = db.rollback();
    if (!rbResult) {
      qCritical() << "[PdfWorker] Chunk transaction rollback failed:"
                  << rbResult.error();
    }
    return false;
  }

  auto commitResult = db.commit();
  if (!commitResult) {
    qCritical() << "[PdfWorker] Chunk transaction commit failed:"
                << commitResult.error();
    (void)db.rollback();
    return false;
  }

  auto wpResult = db.exec("PRAGMA wal_checkpoint(TRUNCATE)");
  if (!wpResult) {
    qCritical() << "[PdfWorker] WAL checkpoint failed:" << wpResult.error();
  }
  return true;
}

/// Delete the Summaries rows of papers whose indexing failed, so that a
/// later import does not skip them as already known.
void PdfWorker::removePapers(const QList<SummaryRow> &rows,
                             const QString &dbPath) {
  if (rows.isEmpty())
    return;

  SqliteDb db;
  auto openResult = db.open(dbPath);
  if (!openResult) {
    qCritical() << "[PdfWorker] Failed to open database to remove papers:"
                << openResult.error();
    return;
  }

  SqliteStatement stmt =
      db.prepare("DELETE FROM Summaries WHERE paper_hash = ?");
  if (!stmt.isValid()) {
    qCritical() << "[PdfWorker] Prepare failed for paper removal";
    return;
  }
  for (const auto &row : rows) {
    auto bindResult = stmt.bind(1, row.paperHash);
    auto execResult = bindResult ? stmt.exec() : bindResult;
    if (!execResult)
      qCritical() << "[PdfWorker] Failed to remove paper" << row.title << ":"
                  << execResult.error();
    else
      qWarning() << "[PdfWorker] Removed unindexed paper:"
                 << row.paperHash.left(12) << row.title;
    stmt.resetAndClearBindings();
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
PdfWorker::parallelHasher(const QList<QUrl> &fileList) {
  QMap<QString, QUrl> hashToUrl;

  // Pool threads emit stepProgress: queued to the controller's thread.
  std::atomic<int> hashed{0};
  const int total = fileList.size();
  QList<QPair<QString, QUrl>> hashUrlPairs = QtConcurrent::blockingMapped(
      fileList, [this, &hashed, total](const QUrl &url) {
        // Once stopped, skip the remaining files: the caller checks the flag.
        if (m_stopFlag.load())
          return qMakePair(QString(), url);
        auto pair = qMakePair(computeFileHash(url), url);
        emit stepProgress(QStringLiteral("hash"), ++hashed, total,
                          QFileInfo(url.toLocalFile()).fileName());
        return pair;
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
//    5. Render the page image (kPageImageDpi) for the UI and visual mode.
//    6. Accumulate full-text and a pageMap for cross-page chunking.
// ============================================================================

/// Resolution of the stored page PNGs (IMG/<hash>_page_NNN.png), shown in
/// the PDF tab.  200 DPI keeps body text sharp up to ~fit-to-width on large
/// screens (the tab caps zoom at one PNG pixel per screen pixel).  Vision
/// models never see this resolution: PageImage::forModel() scales pages down
/// to PageImage::kModelMaxPixels.  ~650 KB per page (150 DPI: ~370 KB).
static constexpr float kPageImageDpi = 200.0f;

ExtractionResult PdfWorker::extractText(const QUrl &fileUrl,
                                        const QString &imgPath) {
  QString localPath = fileUrl.toLocalFile();
  ExtractionResult result;
  result.fileName = QFileInfo(localPath).fileName();
  if (result.fileName.isEmpty())
    result.fileName = fileUrl.toString();

  if (localPath.isEmpty()) {
    result.error = tr("invalid path");
    return result;
  }

  fz_context *ctx = nullptr;
  fz_document *doc = nullptr;

  ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
  if (!ctx) {
    result.error = tr("PDF reader initialisation failed");
    return result;
  }

  fz_try(ctx) { fz_register_document_handlers(ctx); }
  fz_catch(ctx) {
    fz_drop_context(ctx);
    result.error = tr("PDF reader initialisation failed");
    return result;
  }

  fz_try(ctx) { doc = fz_open_document(ctx, localPath.toUtf8().constData()); }
  fz_catch(ctx) {
    fz_drop_context(ctx);
    result.error = tr("the file cannot be opened (damaged or not a PDF?)");
    return result;
  }

  int pageCount = 0;
  float zoom = kPageImageDpi / 72.0f;
  fz_matrix ctm = fz_scale(zoom, zoom);

  fz_try(ctx) { pageCount = fz_count_pages(ctx, doc); }
  fz_catch(ctx) {
    fz_drop_document(ctx, doc);
    fz_drop_context(ctx);
    result.error = tr("the pages cannot be read (damaged file?)");
    return result;
  }

  result.pageCount = pageCount;
  result.paperHash = computeFileHash(fileUrl);
  result.title = QFileInfo(localPath).baseName();
  result.pageMetadata.reserve(pageCount);

  QString fullText;
  QVector<quint16> pageMap; // char offset → page number (compact: 2 bytes)

  for (int i = 0; i < pageCount; ++i) {
    // The caller drops the whole batch once stopped: no need to finish.
    if (m_stopFlag.load())
      break;
    // Scanned pages go through OCR and can take seconds each: report pages,
    // not just files, so long documents visibly progress.
    emit stepProgress(QStringLiteral("extract"), m_extractDone, m_extractTotal,
                      tr("%1 — page %2/%3")
                          .arg(result.fileName)
                          .arg(i + 1)
                          .arg(pageCount));

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
      // Never break/return out of fz_try (it corrupts MuPDF's exception
      // stack): throw instead, fz_catch below skips this page.
      if (!page)
        fz_throw(ctx, FZ_ERROR_GENERIC, "fz_load_page failed for page %d",
                 i + 1);
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
      fz_table_hunt_options huntOpts;
      fz_init_table_hunt_options(ctx, &huntOpts);
      fz_table_hunt(ctx, textPage, &huntOpts);

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
      // 2. Page image (kPageImageDpi, RGB)
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
                           const QString &imgPath) {
  QList<ExtractionResult> results;
  for (const auto &url : filePaths) {
    if (m_stopFlag.load())
      break;
    results.append(extractText(url, imgPath));
    if (m_stopFlag.load())
      break;
    ++m_extractDone;
    emit stepProgress(QStringLiteral("extract"), m_extractDone, m_extractTotal,
                      results.last().fileName);
  }
  return results;
}
