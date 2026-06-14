#include "headers/ingestionengine.h"
#include "headers/summary.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMetaObject>
extern "C" {
#include <mupdf/fitz.h>
}

PdfWorker::PdfWorker(SummaryModel *summaryModel, QObject *parent)
    : QObject(parent), m_summaryModel(summaryModel), m_totalFiles(0),
      m_completedFiles(0) {}

PdfWorker::~PdfWorker() = default;

void PdfWorker::processFiles(const QStringList &files, const QString &imgDir) {
  if (files.isEmpty()) {
    emit status("No PDF files to ingest.");
    return;
  }

  m_cancelled.store(false);
  m_files = files;
  m_totalFiles = files.size();
  m_completedFiles = 0;

  for (int i = 0; i < m_totalFiles; ++i) {
    if (m_cancelled.load()) {
      emit status("Ingestion cancelled.");
      emit finished();
      return;
    }
    ingestPdf(files[i], imgDir, i);
  }

  emit progress(100);
  emit status("Ingestion complete.");
  emit finished();
}

void PdfWorker::processFolder(const QString &folder, const QString &imgDir) {
  processFiles(findPdfFiles(folder), imgDir);
}

void PdfWorker::cancel() { m_cancelled.store(true); }

void PdfWorker::ingestPdf(const QString &pdfPath, const QString &imgDir,
                          int index) {
  QFileInfo fi(pdfPath);
  QString baseName = fi.baseName();

  fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
  if (!ctx) {
    emit status("[ERROR] Cannot create MuPDF context");
    return;
  }
  fz_register_document_handlers(ctx);

  fz_document *doc = fz_open_document(ctx, pdfPath.toUtf8().constData());
  if (!doc) {
    emit status("[ERROR] Cannot open " + pdfPath);
    fz_drop_context(ctx);
    return;
  }

  int pageCount = fz_count_pages(ctx, doc);
  emit status("Processing " + baseName + " (" + QString::number(pageCount) +
              " pages)");

  QDir(imgDir).mkpath(".");

  QString paperHash = computeFileHash(pdfPath);

  for (int i = 0; i < pageCount; ++i) {
    if (m_cancelled.load()) {
      fz_drop_document(ctx, doc);
      fz_drop_context(ctx);
      return;
    }

    fz_page *page = fz_load_page(ctx, doc, i);

    fz_matrix ctm = fz_scale(150.0f / 72.0f, 150.0f / 72.0f);
    fz_pixmap *pixmap = fz_new_pixmap_from_page_number(
        ctx, doc, i, ctm, fz_device_rgb(ctx), false);

    if (pixmap) {
      int w = fz_pixmap_width(ctx, pixmap);
      int h = fz_pixmap_height(ctx, pixmap);
      int stride = fz_pixmap_stride(ctx, pixmap);
      unsigned char *samples = fz_pixmap_samples(ctx, pixmap);

      QImage img(samples, w, h, stride, QImage::Format_RGB888);
      img = img.copy();

      QString imgPath =
          imgDir + "/" + baseName + "_page_" + QString::number(i + 1) + ".png";
      img.save(imgPath, "PNG");
      fz_drop_pixmap(ctx, pixmap);
    }

    fz_drop_page(ctx, page);

    int filePct = (i + 1) * 100 / pageCount;
    int overall =
        (index * 100 / m_totalFiles) + (filePct * 100 / (m_totalFiles * 100));
    emit progress(overall);
  }

  fz_drop_document(ctx, doc);
  fz_drop_context(ctx);

  SummaryRow row;
  row.paperHash = paperHash;
  row.title = baseName;
  row.pagesNum = pageCount;
  row.date = QDateTime::currentDateTime();

  // Insert into SummaryModel on the main thread (it's a QObject living there)
  QMetaObject::invokeMethod(
      m_summaryModel,
      [sm = m_summaryModel, r = std::move(row)]() { sm->insert(r); },
      Qt::QueuedConnection);

  ++m_completedFiles;
  emit status("Done: " + baseName);
  emit status2("Ingested " + QString::number(m_completedFiles) + "/" +
               QString::number(m_totalFiles));
}

QString PdfWorker::computeFileHash(const QString &path) const {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  QCryptographicHash hash(QCryptographicHash::Md5);
  while (!file.atEnd())
    hash.addData(file.read(65536));
  return hash.result().toHex();
}

QStringList PdfWorker::findPdfFiles(const QString &folder) const {
  QStringList pdfs;
  QDirIterator it(folder, {QStringLiteral("*.pdf"), QStringLiteral("*.PDF")},
                  QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    pdfs << it.filePath();
  }
  return pdfs;
}
