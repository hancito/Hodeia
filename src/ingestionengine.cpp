#include "headers/ingestionengine.h"
#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QtConcurrent>
extern "C" {
#include <mupdf/fitz.h>
}

PdfWorker::PdfWorker(QObject *parent) : QObject(parent) {}

PdfWorker::~PdfWorker() = default;

void PdfWorker::stop() {
  // Le worker vérifie m_stopFlag pendant l'ingestion pour s'arrêter proprement.
  // (Implémentation à compléter selon la logique d'arrêt souhaitée.)
}

void PdfWorker::processPDF(const QList<QUrl> &filePath, const QString &imgPath,
                           const QString &dbPath) {
  emit started(filePath.size());
  qInfo() << "[PdfWorker] processPDF started with" << filePath.size()
          << "files";

  QList<ExtractionResult> extractions = parallelExtract(filePath, imgPath);
  QList<SummaryRow> rowsToSave;

  for (const auto &extraction : extractions) {
    if (extraction.extractedText.startsWith("Error:")) {
      qCritical() << "[PdfWorker] Skipping file due to error:"
                  << extraction.extractedText;
      continue;
    }

    SummaryRow row;
    row.paperHash = extraction.paperHash;
    row.title = extraction.title;
    row.pagesNum = extraction.pageCount;
    row.date = QDateTime::currentDateTime();

    // Store the extracted text in summaryJson so it is saved to the DB
    QJsonObject json;
    json["raw_text"] = extraction.extractedText;
    row.summaryJson = QJsonDocument(json).toJson();

    rowsToSave.append(row);
  }

  // Save everything in one batch transaction
  if (!rowsToSave.isEmpty()) {
    savePapers(rowsToSave, dbPath);
  }

  emit finished();
  qInfo() << "[PdfWorker] finished signal emitted";
}

void PdfWorker::savePapers(const QList<SummaryRow> &rows,
                           const QString &dbPath) {
  if (rows.isEmpty())
    return;

  // 1. Generate a strictly unique connection name for THIS specific run/thread
  // execution
  static QAtomicInt counter = 0;
  QString connectionName =
      QString("worker_conn_%1_%2")
          .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()))
          .arg(counter.fetchAndAddRelaxed(1));

  {
    QSqlDatabase db;
    if (QSqlDatabase::contains(connectionName)) {
      db = QSqlDatabase::database(connectionName);
    } else {
      db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
      db.setDatabaseName(dbPath);
    }

    if (!db.open()) {
      qCritical() << "[PdfWorker] Failed to open database for saving in thread"
                  << QThread::currentThreadId() << ":" << db.lastError().text();
      return;
    }

    // Start transaction for bulk insertion speed and data atomicity
    if (!db.transaction()) {
      qCritical() << "[PdfWorker] Failed to start transaction:"
                  << db.lastError().text();
      return;
    }

    QSqlQuery query(db);
    query.prepare("INSERT INTO Summaries (paper_hash, title, authors, "
                  "pages_num, journal, year, topics, model, "
                  "summary_json, date) "
                  "VALUES (:hash, :title, :authors, :pages, :journal, :year, "
                  ":topics, :model, :json, :date)");

    for (const auto &row : rows) {
      query.bindValue(":hash", row.paperHash);
      query.bindValue(":title", row.title);
      query.bindValue(":authors", row.authors);
      query.bindValue(":pages", row.pagesNum);
      query.bindValue(":journal", row.journal);
      query.bindValue(":year", row.year);
      query.bindValue(":topics", row.topics);
      query.bindValue(":model", row.model);
      query.bindValue(":json", row.summaryJson.toString());
      query.bindValue(":date", row.date);

      if (!query.exec()) {
        qCritical() << "[PdfWorker] Error inserting paper" << row.title << ":"
                    << query.lastError().text();
        db.rollback(); // Rollback entire batch if an insertion fails
        return;
      }
    }

    if (db.commit()) {
      qInfo() << "[PdfWorker] Successfully batch saved" << rows.size()
              << "papers.";
    } else {
      qCritical() << "[PdfWorker] Transaction commit failed:"
                  << db.lastError().text();
      db.rollback();
    }
    db.close();
  }
  // Safely remove the unique connection from Qt's global storage
  QSqlDatabase::removeDatabase(connectionName);
}

QStringList PdfWorker::getAllPaperHashes(const QString &dbPath) const {
  QStringList hashes;
  QString connectionName = "reader_conn_" + QString::number(qHash(dbPath));

  {
    QSqlDatabase db;
    if (QSqlDatabase::contains(connectionName)) {
      db = QSqlDatabase::database(connectionName);
    } else {
      db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
      db.setDatabaseName(dbPath);
    }

    if (!db.open()) {
      qCritical() << "[PdfWorker] Failed to open database for reading:"
                  << db.lastError().text();
      return hashes;
    }

    QSqlQuery query(db);
    if (query.exec("SELECT paper_hash FROM Summaries")) {
      while (query.next()) {
        hashes << query.value(0).toString();
      }
    } else {
      qCritical() << "[PdfWorker] Error fetching hashes:"
                  << query.lastError().text();
    }
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

  // Calculate MD5 hash (you can change this to Sha256 if preferred)
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
    result.extractedText = "Error: Invalid path.";
    return result;
  }

  fz_context *ctx = nullptr;
  fz_document *doc = nullptr;

  // 1. Create MuPDF context
  ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
  if (!ctx) {
    result.extractedText = "Error: Cannot register MuPDF context.";
    return result;
  }

  // Register default document handlers (PDF, XPS, CBZ, etc.)
  fz_try(ctx) { fz_register_document_handlers(ctx); }
  fz_catch(ctx) {
    fz_drop_context(ctx);
    result.extractedText = "Error: Failed to register document handlers.";
    return result;
  }

  // 2. Open the PDF document
  fz_try(ctx) { doc = fz_open_document(ctx, localPath.toUtf8().constData()); }
  fz_catch(ctx) {
    fz_drop_context(ctx);
    result.extractedText = "Error: Cannot open document " + localPath;
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
    result.extractedText = "Error: Cannot count pages.";
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

      // Create a text-extracting device that feeds into our textPage structure
      dev = fz_new_stext_device(ctx, textPage, nullptr);

      // Run the page through the device to extract text
      fz_run_page(ctx, page, dev, fz_identity, nullptr);

      // Close the device immediately after use
      fz_close_device(ctx, dev);
      // Tax the image of the page
      pix = fz_new_pixmap_from_page_number(ctx, doc, i, ctm, fz_device_rgb(ctx),
                                           0);
      // 5. Wrap MuPDF raw pixel data into a Qt QImage
      // pix->samples contains raw RGB byte streams.
      // pix->stride represents the total byte length of a single horizontal
      // row.
      QImage img(pix->samples, pix->w, pix->h, pix->stride,
                 QImage::Format_RGB888);
      img = img.copy(); // detach: MuPDF will drop pix below

      // Define output file name prefixed with the PDF base name to avoid
      // collisions across files
      // QString baseName = QFileInfo(localPath).baseName();
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

      // Drop the pixmap allocation for this individual page loop
      fz_drop_pixmap(ctx, pix);
      // Fetch the layout text from MuPDF as a string block
      fz_buffer *buf = fz_new_buffer(ctx, 256);
      fz_output *out = fz_new_output_with_buffer(ctx, buf);
      fz_print_stext_page_as_text(ctx, out, textPage);
      fz_close_output(ctx, out);

      // Append page content to our return variable
      const char *pageText = fz_string_from_buffer(ctx, buf);
      result.extractedText.append(QString::fromUtf8(pageText));
      result.extractedText.append("\n--- Page Break ---\n");

      // Clean up buffers for this specific page loop
      fz_drop_buffer(ctx, buf);
    }
    fz_always(ctx) {
      // Clean up structures safely even if exceptions occurred
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

void PdfWorker::filterExistingHashes(QMap<QString, QUrl> &hashesToProcess,
                                     const QString &dbPath) const {
  QStringList existingHashes = getAllPaperHashes(dbPath);
  if (existingHashes.isEmpty()) {
    return;
  }

  for (const QString &hash : existingHashes) {
    hashesToProcess.remove(hash);
  }

  qInfo() << "[PdfWorker] Filtered" << existingHashes.size()
          << "existing hashes. Remaining to process:" << hashesToProcess.size();
}
