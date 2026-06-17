#include "headers/ingestionengine.h"
#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QImage>
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
  // QStringList results = multiExtractText(filePath, imgPath, dbPath);
  QStringList results = parallelExtract(filePath, imgPath);
  qInfo() << "[PdfWorker] multiExtractText done, results size:"
          << results.size() << results;
  emit finished();
  qInfo() << "[PdfWorker] finished signal emitted";
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

QStringList PdfWorker::parallelHasher(const QList<QUrl> &fileList) const {
  QStringList hashList = QtConcurrent::blockingMapped(fileList, getFileHash);
  return hashList;
}

QString PdfWorker::extractText(const QUrl &fileUrl,
                               const QString &imgPath) const {
  QString localPath = fileUrl.toLocalFile();

  if (localPath.isEmpty())
    return "Error: Invalid path.";

  fz_context *ctx = nullptr;
  fz_document *doc = nullptr;
  QString extractedText = "";

  // 1. Create MuPDF context
  ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
  if (!ctx) {
    return "Error: Cannot register MuPDF context.";
  }

  // Register default document handlers (PDF, XPS, CBZ, etc.)
  fz_try(ctx) { fz_register_document_handlers(ctx); }
  fz_catch(ctx) {
    fz_drop_context(ctx);
    return "Error: Failed to register document handlers.";
  }

  // 2. Open the PDF document
  fz_try(ctx) { doc = fz_open_document(ctx, localPath.toUtf8().constData()); }
  fz_catch(ctx) {
    fz_drop_context(ctx);
    return "Error: Cannot open document " + localPath;
  }

  // 3. Count total pages and loop through them
  int pageCount = 0;
  float zoom = 150.0f / 72.0f;
  fz_matrix ctm = fz_scale(zoom, zoom);
  fz_try(ctx) { pageCount = fz_count_pages(ctx, doc); }
  fz_catch(ctx) {
    fz_drop_document(ctx, doc);
    fz_drop_context(ctx);
    return "Error: Cannot count pages.";
  }

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
      QString baseName = getFileHash(fileUrl);
      QString fileName =
          QString("%1_page_%2.png").arg(baseName).arg(i + 1, 3, 10, QChar('0'));
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
      extractedText.append(QString::fromUtf8(pageText));
      extractedText.append("\n--- Page Break ---\n");

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

  return extractedText;
}

QStringList PdfWorker::multiExtractText(const QList<QUrl> &filePaths,
                                        const QString &imgPath,
                                        const QString &dbPath) const {
  QStringList results;
  for (int i = 0; i < filePaths.size(); ++i) {
    const QUrl &fileUrl = filePaths[i];
    qInfo() << "[PdfWorker] Processing file" << i + 1 << "/" << filePaths.size()
            << fileUrl;
    results << extractText(fileUrl, imgPath);
  }
  return results;
}

QStringList PdfWorker::parallelExtract(const QList<QUrl> &filePaths,
                                       const QString &imgPath) const {
  // FIX 1: Use blockingMapped instead of blockingMap to return a new collection
  QStringList textList =
      QtConcurrent::blockingMapped(filePaths, [this, imgPath](const QUrl &url) {
        // FIX 2: Added 'this' to the capture clause (since extractText is a
        // member function) FIX 3: Added 'return' and the missing semicolon ';'
        return this->extractText(url, imgPath);
      });

  return textList;
}
