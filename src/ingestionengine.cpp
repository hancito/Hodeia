#include "headers/ingestionengine.h"
#include "headers/sqlitedb.h"
#include "headers/sqlitevec.h"
#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QtConcurrent>

extern "C" {
#include <mupdf/fitz.h>
}

// ─── Text cleaning ───────────────────────────────────────────────

QString PdfWorker::cleanPdfText(const QString &raw) const {
  // NFKC expands PDF ligatures (ﬁ → fi, ﬂ → fl) into base characters.
  QString text = raw.normalized(QString::NormalizationForm_KC);
  // Soft hyphens inserted by line-wrapping would split words incorrectly.
  text.remove(QChar(0x00AD));
  // Collapse multiple whitespace (spaces, tabs, stray newlines) into one.
  QRegularExpression multiSpace(R"(\s+)");
  text.replace(multiSpace, QString(" "));
  return text.trimmed();
}

// ─── Chunking ────────────────────────────────────────────────────

QList<TextChunk> PdfWorker::chunkDocumentText(const QString &fullText,
                                              const QVector<quint16> &pageMap,
                                              const QString &paperHash) const {
  QList<TextChunk> chunks;

  // 1) Sentence segmentation — split after . ! ? followed by whitespace.
  QStringList sentences;
  int lastEnd = 0;
  QRegularExpression sentRe(R"((?<=[.!?])\s+)");
  auto matchIter = sentRe.globalMatch(fullText);
  while (matchIter.hasNext()) {
    auto match = matchIter.next();
    QString sentence = fullText.mid(lastEnd, match.capturedStart() - lastEnd +
                                                 match.capturedLength());
    if (!sentence.trimmed().isEmpty())
      sentences.append(sentence.trimmed());
    lastEnd = match.capturedEnd();
  }
  if (lastEnd < fullText.length()) {
    QString remainder = fullText.mid(lastEnd).trimmed();
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

  return chunks;
}

// ─── File identity ───────────────────────────────────────────────

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

// ─── Lifecycle ───────────────────────────────────────────────────

PdfWorker::PdfWorker(ConfigManager *config, SqliteVecManager *vecManager,
                     QObject *parent)
    : QObject(parent), m_vecManager(vecManager), m_config(config) {
  // Forward embedding progress from the vector manager to the UI.
  if (m_vecManager) {
    connect(m_vecManager, &SqliteVecManager::embeddingProgress, this,
            &PdfWorker::embeddingProgress);
  }
}

PdfWorker::~PdfWorker() = default;

void PdfWorker::stop() { m_stopFlag.store(true); }

// ─── Main ingestion pipeline ─────────────────────────────────────

void PdfWorker::processPDF(const QList<QUrl> &filePath, const QString &imgPath,
                           const QString &dbPath) {
  m_stopFlag.store(false);
  emit started();

  // Deduplicate files by content hash, skip papers already in the DB.
  QString message =
      "Started with " + QString::number(filePath.size()) + " files";
  emit status(message);

  QMap<QString, QUrl> hashesToProcess = parallelHasher(filePath);
  QList<QUrl> urlsToProcess = filterExistingHashes(hashesToProcess, dbPath);
  message = "Skipped " +
            QString::number(filePath.size() - urlsToProcess.size()) + " papers";
  emit status(message);

  // Process in small batches to keep memory usage bounded and provide
  // incremental progress feedback to the UI.
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

    // Extract text + images in parallel across files in the batch.
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

      // Move chunks out to free memory in extraction result early.
      allChunks << std::move(extraction.chunks);
    }

    // Persist metadata and vector embeddings in separate transactions.
    if (!rows.isEmpty()) {
      savePapers(rows, dbPath);
      savedCount += rows.size();
    }

    if (!allChunks.isEmpty()) {
      QString statusMsg =
          QString("Embedding %1 chunks...").arg(allChunks.size());
      emit status(statusMsg);

      if (m_vecManager) {
        // insertChunks() has its own lazy-load guard — it will call
        // initialize() if m_ready is false.
        ingestChunks(allChunks, dbPath);
      } else {
        emit status("Warning: embedding model not loaded, skipping vector "
                    "storage");
      }

      allChunks.clear();
      allChunks.squeeze();
    }

    message =
        QString("Saved %1/%2 papers").arg(savedCount + errorCount).arg(total);
    emit status(message);
  }

  if (errorCount > 0)
    qWarning() << "[PdfWorker] Total skipped due to errors:" << errorCount;

  emit finished();
}

// ─── Database writes ─────────────────────────────────────────────

void PdfWorker::ingestChunks(const QList<TextChunk> &chunks,
                             const QString &dbPath) {
  if (chunks.isEmpty())
    return;

  SqliteDb db;
  if (!db.open(dbPath)) {
    qCritical() << "[PdfWorker] Failed to open database for chunk ingestion:"
                << db.lastError();
    return;
  }

  // Single transaction for the entire batch — much faster than per-row commits.
  if (!db.transaction()) {
    qCritical() << "[PdfWorker] Failed to start transaction for chunks:"
                << db.lastError();
    return;
  }

  int inserted = m_vecManager->insertChunks(db, chunks);

  if (inserted < 0) {
    if (!db.rollback()) {
      qCritical() << "[PdfWorker] Chunk transaction rollback failed:"
                  << db.lastError();
    }
    return;
  }

  if (!db.commit()) {
    qCritical() << "[PdfWorker] Chunk transaction commit failed:"
                << db.lastError();
    db.rollback();
  }

  // Truncate the WAL file so it doesn't grow unbounded across batches.
  db.exec("PRAGMA wal_checkpoint(TRUNCATE)");
}

void PdfWorker::savePapers(const QList<SummaryRow> &rows,
                           const QString &dbPath) {
  if (rows.isEmpty())
    return;

  SqliteDb db;
  if (!db.open(dbPath)) {
    qCritical() << "[PdfWorker] Failed to open database for saving in thread"
                << QThread::currentThreadId() << ":" << db.lastError();
    return;
  }

  if (!db.transaction()) {
    qCritical() << "[PdfWorker] Failed to start transaction:" << db.lastError();
    return;
  }

  SqliteStatement stmt =
      db.prepare("INSERT INTO Summaries (paper_hash, title, "
                 "authors, "
                 "pages_num, journal, year, topics, llmodel, "
                 "summary_json, date) "
                 "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");

  for (const auto &row : rows) {
    stmt.bind(1, row.paperHash);
    stmt.bind(2, row.title);
    stmt.bind(3, row.authors);
    stmt.bind(4, row.pagesNum);
    stmt.bind(5, row.journal);
    stmt.bind(6, row.year);
    stmt.bind(7, row.topics);
    stmt.bind(8, row.llmodel);
    stmt.bind(9, row.summaryJson.toByteArray());
    stmt.bind(10, row.date);

    if (!stmt.exec()) {
      qCritical() << "[PdfWorker] Error inserting paper" << row.title << ":"
                  << stmt.errorMsg();
      db.rollback();
      return;
    }
    stmt.resetAndClearBindings();
  }

  if (!db.commit()) {
    qCritical() << "[PdfWorker] Transaction commit failed:" << db.lastError();
    db.rollback();
  }

  db.exec("PRAGMA wal_checkpoint(TRUNCATE)");
}

// ─── Hash helpers ────────────────────────────────────────────────

QStringList PdfWorker::getAllPaperHashes(const QString &dbPath) const {
  QStringList hashes;

  SqliteDb db;
  if (!db.open(dbPath)) {
    qCritical() << "[PdfWorker] Failed to open database for reading:"
                << db.lastError();
    return hashes;
  }

  SqliteStatement stmt = db.prepare("SELECT paper_hash FROM Summaries");
  while (stmt.step()) {
    hashes << stmt.columnText(0);
  }
  return hashes;
}

QMap<QString, QUrl>
PdfWorker::parallelHasher(const QList<QUrl> &fileList) const {
  QMap<QString, QUrl> hashToUrl;

  // Compute MD5 hashes across all available cores.
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

// ─── PDF extraction (MuPDF) ──────────────────────────────────────

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
  float zoom = 150.0f / 72.0f;
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

  // Accumulate full document text across all pages and a parallel
  // pageMap (char offset -> page number) so chunking can cross
  // page boundaries without losing semantic continuity.
  QString fullText;
  QVector<quint16> pageMap; // quint16: 2 bytes/char vs 4 for int

  for (int i = 0; i < pageCount; ++i) {
    fz_page *page = nullptr;
    fz_stext_page *textPage = nullptr;
    fz_device *dev = nullptr;
    fz_pixmap *pix = nullptr;

    fz_try(ctx) {
      page = fz_load_page(ctx, doc, i);
      textPage = fz_new_stext_page(ctx, fz_bound_page(ctx, page));
      dev = fz_new_stext_device(ctx, textPage, nullptr);
      fz_run_page(ctx, page, dev, fz_identity, nullptr);
      fz_close_device(ctx, dev);

      // --- Render page thumbnail ---
      pix = fz_new_pixmap_from_page_number(ctx, doc, i, ctm, fz_device_rgb(ctx),
                                           0);
      QImage img(pix->samples, pix->w, pix->h, pix->stride,
                 QImage::Format_RGB888);
      img = img.copy(); // detach before MuPDF drops the pixmap

      QString hash = computeFileHash(fileUrl);
      QString fileName =
          QString("%1_page_%2.png").arg(hash).arg(i + 1, 3, 10, QChar('0'));
      QString fullOutputPath = QDir(imgPath).filePath(fileName);

      if (!img.save(fullOutputPath, "PNG")) {
        qCritical() << "Failed to save image to:" << fullOutputPath;
      }

      fz_drop_pixmap(ctx, pix);
      pix = nullptr;

      // --- Extract page text ---
      fz_buffer *buf = fz_new_buffer(ctx, 256);
      fz_output *out = fz_new_output_with_buffer(ctx, buf);
      fz_print_stext_page_as_text(ctx, out, textPage);
      fz_close_output(ctx, out);
      fz_drop_output(ctx, out);

      // fz_buffer_storage returns both the length and a pointer to the
      // raw bytes — NOT null-terminated, so we must pass the length.
      unsigned char *bufData = nullptr;
      size_t bufLen = fz_buffer_storage(ctx, buf, &bufData);
      QString pageTextStr = QString::fromUtf8(
          reinterpret_cast<const char *>(bufData), static_cast<int>(bufLen));

      // Clean and append to the running document text.
      pageTextStr = cleanPdfText(pageTextStr);

      if (i > 0) {
        fullText.append('\n');
        pageMap.push_back(i);
      }
      for (int j = 0; j < pageTextStr.length(); ++j) {
        pageMap.push_back(i);
      }
      fullText.append(pageTextStr);

      fz_drop_buffer(ctx, buf);
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
    fz_catch(ctx) { qWarning() << "Error extracting page" << i; }
  }

  // Chunk the full document text across page boundaries.
  result.chunks = chunkDocumentText(fullText, pageMap, result.paperHash);

  fz_drop_document(ctx, doc);
  fz_drop_context(ctx);

  return result;
}

// ─── Parallel dispatch ───────────────────────────────────────────

QList<ExtractionResult>
PdfWorker::parallelExtract(const QList<QUrl> &filePaths,
                           const QString &imgPath) const {
  // Each file is extracted in its own thread.
  return QtConcurrent::blockingMapped(filePaths,
                                      [this, imgPath](const QUrl &url) {
                                        return this->extractText(url, imgPath);
                                      });
}
