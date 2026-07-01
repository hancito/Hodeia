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

namespace {
constexpr int MaxChunkChars = 1500;
constexpr int OverlapChars = 200;
} // namespace

extern "C" {
#include <mupdf/fitz.h>
}

PdfWorker::PdfWorker(ConfigManager *config, SqliteVecManager *vecManager,
                     QObject *parent)
    : QObject(parent), m_vecManager(vecManager), m_config(config) {
  if (m_vecManager) {
    connect(m_vecManager, &SqliteVecManager::embeddingProgress, this,
            &PdfWorker::embeddingProgress);
  }
}

PdfWorker::~PdfWorker() = default;

void PdfWorker::stop() {
  // Le worker vérifie m_stopFlag pendant l'ingestion pour s'arrêter proprement.
  // (Implémentation à compléter selon la logique d'arrêt souhaitée.)
}

void PdfWorker::processPDF(const QList<QUrl> &filePath, const QString &imgPath,
                           const QString &dbPath) {
  emit started();
  QString message =
      "Started with " + QString::number(filePath.size()) + " files";
  emit status(message);
  QMap<QString, QUrl> hashesToProcess = parallelHasher(filePath);
  QList<QUrl> urlsToProcess = filterExistingHashes(hashesToProcess, dbPath);
  message = "Skipped " +
            QString::number(filePath.size() - urlsToProcess.size()) + " papers";
  emit status(message);

  const int chunkSize = 5;
  int savedCount = 0;
  int errorCount = 0;
  int total = urlsToProcess.size();

  for (int i = 0; i < total; i += chunkSize) {
    int end = qMin(i + chunkSize, total);
    QList<QUrl> chunkUrls(urlsToProcess.mid(i, end - i));

    message = QString("Extracting %1-%2/%3").arg(i + 1).arg(end).arg(total);
    emit status(message);

    QList<ExtractionResult> extractions = parallelExtract(chunkUrls, imgPath);

    QList<SummaryRow> chunk;
    QList<TextChunk> allChunks;

    for (const auto &extraction : extractions) {

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

      chunk.append(row);

      // Chunks were already built during extraction
      allChunks.append(extraction.chunks.begin(), extraction.chunks.end());
    }

    if (!chunk.isEmpty()) {
      savePapers(chunk, dbPath);
      savedCount += chunk.size();
    }

    // Ingest vector chunks (embeddings + vec0 insert)
    if (!allChunks.isEmpty()) {
      QString statusMsg =
          QString("Embedding %1 chunks...").arg(allChunks.size());
      emit status(statusMsg);

      if (m_vecManager && m_vecManager->isReady()) {
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
  qInfo() << "[PdfWorker] Ingestion complete: saved" << savedCount
          << "errors:" << errorCount;

  emit finished();
  qInfo() << "[PdfWorker] finished signal emitted";
}

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

  if (!db.transaction()) {
    qCritical() << "[PdfWorker] Failed to start transaction for chunks:"
                << db.lastError();
    return;
  }

  int inserted = m_vecManager->insertChunks(db, chunks);

  if (inserted < 0) {
    // insertChunks returned a negative error code; roll back
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

  qInfo() << "[PdfWorker] Inserted" << inserted << "vector chunks";
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
}

QStringList PdfWorker::getAllPaperHashes(const QString &dbPath) const {
  QStringList hashes;

  SqliteDb db;
  if (!db.open(dbPath)) {
    qCritical() << "[PdfWorker] Failed to open "
                   "database for reading:"
                << db.lastError();
    return hashes;
  }

  SqliteStatement stmt = db.prepare("SELECT paper_hash FROM Summaries");
  while (stmt.step()) {
    hashes << stmt.columnText(0);
  }
  return hashes;
}

QString getFileHash(const QUrl &filePath) {
  QString localPath = filePath.toLocalFile();
  if (localPath.isEmpty()) {
    return QString("Error: Invalid local file path.");
  }
  QFile file(localPath);
  if (!file.open(QIODevice::ReadOnly)) {
    return QString("Error: Could not open file.");
  }

  // Calculate MD5 hash (you can change this to
  // Sha256 if preferred)
  QCryptographicHash decrypter(QCryptographicHash::Md5);
  if (decrypter.addData(&file)) {
    return QString(decrypter.result().toHex());
  }
  return QString("Error: Hash calculation failed.");
}

QMap<QString, QUrl>
PdfWorker::parallelHasher(const QList<QUrl> &fileList) const {
  QMap<QString, QUrl> hashToUrl;

  QList<QPair<QString, QUrl>> hashUrlPairs =
      QtConcurrent::blockingMapped(fileList, [](const QUrl &url) {
        return qMakePair(getFileHash(url), url);
      });

  for (const auto &pair : hashUrlPairs) {
    hashToUrl.insert(pair.first, pair.second);
  }

  return hashToUrl;
}

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

  // 1. Create MuPDF context
  ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
  if (!ctx) {
    result.title = "Error: Cannot register MuPDF context.";
    return result;
  }

  // Register default document handlers (PDF, XPS,
  // CBZ, etc.)
  fz_try(ctx) { fz_register_document_handlers(ctx); }
  fz_catch(ctx) {
    fz_drop_context(ctx);
    result.title = "Error: Failed to register "
                   "document handlers.";
    return result;
  }

  // 2. Open the PDF document
  fz_try(ctx) { doc = fz_open_document(ctx, localPath.toUtf8().constData()); }
  fz_catch(ctx) {
    fz_drop_context(ctx);
    result.title = "Error: Cannot open document " + localPath;
    return result;
  }

  // 3. Count total pages and loop through them
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
  result.paperHash = getFileHash(fileUrl);
  result.title = QFileInfo(localPath).baseName();

  for (int i = 0; i < pageCount; ++i) {
    fz_page *page = nullptr;
    fz_stext_page *textPage = nullptr;
    fz_device *dev = nullptr;
    fz_pixmap *pix = nullptr;

    fz_try(ctx) {
      // Load the page
      page = fz_load_page(ctx, doc, i);

      // Create a structure to hold extracted text
      textPage = fz_new_stext_page(ctx, fz_bound_page(ctx, page));

      // Create a text-extracting device that feeds
      // into our textPage structure
      dev = fz_new_stext_device(ctx, textPage, nullptr);

      // Run the page through the device to extract
      // text
      fz_run_page(ctx, page, dev, fz_identity, nullptr);

      // Close the device immediately after use
      fz_close_device(ctx, dev);
      // Tax the image of the page
      pix = fz_new_pixmap_from_page_number(ctx, doc, i, ctm, fz_device_rgb(ctx),
                                           0);
      // 5. Wrap MuPDF raw pixel data into a Qt
      // QImage pix->samples contains raw RGB byte
      // streams. pix->stride represents the total
      // byte length of a single horizontal row.
      QImage img(pix->samples, pix->w, pix->h, pix->stride,
                 QImage::Format_RGB888);
      img = img.copy(); // detach: MuPDF will drop
                        // pix below

      // Define output file name prefixed with the
      // PDF base name to avoid collisions across
      // files QString baseName =
      // QFileInfo(localPath).baseName();
      QString hash = getFileHash(fileUrl);
      QString fileName =
          QString("%1_page_%2.png").arg(hash).arg(i + 1, 3, 10, QChar('0'));
      QString fullOutputPath = QDir(imgPath).filePath(fileName);

      // 6. Save the image via Qt
      if (img.save(fullOutputPath, "PNG")) {
        qInfo() << "Saved:" << fileName;
      } else {
        qCritical() << "Failed to save image to:" << fullOutputPath;
      }

      // Drop the pixmap allocation for this
      // individual page loop
      fz_drop_pixmap(ctx, pix);
      // Fetch the layout text from MuPDF as a string
      // block
      fz_buffer *buf = fz_new_buffer(ctx, 256);
      fz_output *out = fz_new_output_with_buffer(ctx, buf);
      fz_print_stext_page_as_text(ctx, out, textPage);
      fz_close_output(ctx, out);
      fz_drop_output(ctx, out);

      // Extract page text as string
      const char *pageText = fz_string_from_buffer(ctx, buf);
      QString pageTextStr = QString::fromUtf8(pageText);

      // Chunk directly and accumulate
      QList<TextChunk> pageChunks =
          chunkPageText(pageTextStr, result.paperHash, i);
      result.chunks.append(pageChunks.begin(), pageChunks.end());

      // Clean up buffers for this specific page loop
      fz_drop_buffer(ctx, buf);
    }
    fz_always(ctx) {
      // Clean up structures safely even if
      // exceptions occurred
      if (dev)
        fz_drop_device(ctx, dev);
      if (textPage)
        fz_drop_stext_page(ctx, textPage);
      if (page)
        fz_drop_page(ctx, page);
    }
    fz_catch(ctx) { qWarning() << "Error extracting page " << i; }
  }

  // 4. Final Cleanup
  fz_drop_document(ctx, doc);
  fz_drop_context(ctx);

  return result;
}

QList<ExtractionResult>
PdfWorker::parallelExtract(const QList<QUrl> &filePaths,
                           const QString &imgPath) const {
  QList<ExtractionResult> results =
      QtConcurrent::blockingMapped(filePaths, [this, imgPath](const QUrl &url) {
        return this->extractText(url, imgPath);
      });

  return results;
}

QList<TextChunk> PdfWorker::chunkPageText(const QString &pageText,
                                          const QString &paperHash,
                                          int pageNum) const {
  QList<TextChunk> chunks;

  // Split on sentence boundaries
  QStringList sentences;
  int lastEnd = 0;
  QRegularExpression sentRe(R"((?<=[.!?])\s+)");
  auto matchIter = sentRe.globalMatch(pageText);
  while (matchIter.hasNext()) {
    auto match = matchIter.next();
    QString sentence = pageText.mid(lastEnd, match.capturedStart() - lastEnd +
                                                 match.capturedLength());
    if (!sentence.trimmed().isEmpty())
      sentences.append(sentence.trimmed());
    lastEnd = match.capturedEnd();
  }
  if (lastEnd < pageText.length()) {
    QString remainder = pageText.mid(lastEnd).trimmed();
    if (!remainder.isEmpty())
      sentences.append(remainder);
  }

  // Accumulate sentences into chunks with overlap
  QString currentChunk;
  int chunkIdx = 0;

  for (const auto &sent : sentences) {
    QString candidate =
        currentChunk.isEmpty() ? sent : currentChunk + " " + sent;
    if (candidate.length() > MaxChunkChars && !currentChunk.isEmpty()) {
      TextChunk tc;
      tc.text = currentChunk.trimmed();
      tc.paperHash = paperHash;
      tc.page = pageNum;
      tc.chunkIdx = chunkIdx++;
      chunks.append(tc);

      int overlapStart = qMax(0, currentChunk.length() - OverlapChars);
      currentChunk = currentChunk.mid(overlapStart) + " " + sent;
    } else {
      currentChunk = candidate;
    }
  }

  if (!currentChunk.trimmed().isEmpty()) {
    TextChunk tc;
    tc.text = currentChunk.trimmed();
    tc.paperHash = paperHash;
    tc.page = pageNum;
    tc.chunkIdx = chunkIdx++;
    chunks.append(tc);
  }

  return chunks;
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

  qInfo() << "[PdfWorker] Filtered" << existingHashes.size()
          << "existing hashes. Remaining to process:" << urlsToProcess.size();

  return urlsToProcess;
}
