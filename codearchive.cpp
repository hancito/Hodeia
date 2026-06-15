#pragma once
#include "headers/ingestionengine.h"
#include <QObject>
#include <QThread>

class ConfigManager;

class EngineController : public QObject {
  Q_OBJECT
public:
  explicit EngineController(ConfigManager *config, QObject *parent = nullptr)
      : QObject(parent), m_config(config) {}
  ~EngineController() override;

  Q_INVOKABLE void startIngestion(const QStringList &files);
  Q_INVOKABLE void startIngestionFolder(const QString &folder);
  Q_INVOKABLE void ingestFiles(const QStringList &files) {
    startIngestion(files);
  }
  Q_INVOKABLE void ingestFolder(const QString &folder) {
    startIngestionFolder(folder);
  }
  Q_INVOKABLE void cancelActiveOperation();

signals:
  void progressChanged(int current, int total);
  void progress(int value);
  void statusReceived(const QString &text);
  void status2Received(const QString &text);
  void ingestionFinished();

private:
  void setupWorker();
  void stopThread();

  ConfigManager *m_config = nullptr;
  QThread *m_workerThread = nullptr;
  PdfWorker *m_worker = nullptr;
};

#pragma once
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QThread>
#include <atomic>

class SummaryModel;

class PdfWorker : public QObject {
  Q_OBJECT
public:
  explicit PdfWorker(SummaryModel *summaryModel, QObject *parent = nullptr);
  ~PdfWorker();

signals:
  void progress(int value);
  void status(const QString &text);
  void status2(const QString &text);
  void finished();

public slots:
  void processFiles(const QStringList &files, const QString &imgDir);
  void processFolder(const QString &folder, const QString &imgDir);
  void cancel();
  void setCancelled() { m_cancelled.store(true); }

private:
  void ingestPdf(const QString &pdfPath, const QString &imgDir, int index);
  QString computeFileHash(const QString &path) const;
  QStringList findPdfFiles(const QString &folder) const;

  SummaryModel *m_summaryModel = nullptr;

  int m_totalFiles;
  int m_completedFiles;
  QStringList m_files;
  std::atomic<bool> m_cancelled{false};
};

#include "headers/configmanager.h"
#include "headers/enginemanager.h"
#include "headers/summary.h"
#include <QCoreApplication>
#include <QMetaObject>

EngineController::~EngineController() { stopThread(); }

void EngineController::startIngestion(const QStringList &files) {
  if (files.isEmpty())
    return;

  setupWorker();

  QString imgDir = m_config->projectImgPath(m_config->lastProject());
  QMetaObject::invokeMethod(m_worker, "processFiles", Qt::QueuedConnection,
                            Q_ARG(QStringList, files), Q_ARG(QString, imgDir));
}

void EngineController::startIngestionFolder(const QString &folder) {
  setupWorker();

  QString imgDir = m_config->projectImgPath(m_config->lastProject());
  QMetaObject::invokeMethod(m_worker, "processFolder", Qt::QueuedConnection,
                            Q_ARG(QString, folder), Q_ARG(QString, imgDir));
}

void EngineController::cancelActiveOperation() { stopThread(); }

void EngineController::setupWorker() {
  stopThread();

  m_workerThread = new QThread();

  SummaryModel *summaryModel =
      qobject_cast<SummaryModel *>(QCoreApplication::instance()
                                       ->property("summaryModel")
                                       .value<QObject *>());

  m_worker = new PdfWorker(summaryModel);
  m_worker->moveToThread(m_workerThread);

  // Forward signals
  connect(m_worker, &PdfWorker::progress, this, &EngineController::progress,
          Qt::QueuedConnection);
  connect(m_worker, &PdfWorker::status, this, &EngineController::statusReceived,
          Qt::QueuedConnection);
  connect(m_worker, &PdfWorker::status2, this,
          &EngineController::status2Received, Qt::QueuedConnection);
  connect(m_worker, &PdfWorker::finished, this,
          &EngineController::ingestionFinished, Qt::QueuedConnection);

  m_workerThread->start();
}

void EngineController::stopThread() {
  PdfWorker *worker = m_worker;
  QThread *thread = m_workerThread;

  m_worker = nullptr;
  m_workerThread = nullptr;

  if (worker && thread) {
    // 1. Set cancel immediately so the worker loop sees it and exits.
    worker->setCancelled();

    // 2. Queue a single event in the worker thread that deletes the worker
    //    (in its own thread) and then quits the thread.
    QMetaObject::invokeMethod(
        worker,
        [worker, thread]() {
          delete worker; // destroys in worker's own thread
          thread->quit();
        },
        Qt::QueuedConnection);

    // 3. Wait for the thread to finish. The lambda above runs once the
    //    worker's current slot (processFiles) returns.
    thread->wait(5000);

    // 4. If the thread is still running, force it.
    if (!thread->isFinished()) {
      thread->requestInterruption();
      thread->quit();
      thread->wait(2000);
    }

    // 5. Delete the thread directly. After wait(), the thread object itself
    //    is safe to delete from the calling thread.
    delete thread;
  }
}

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

// Bind ingestion signals
Connections {
target:
  ingestionEngine function onProgress(value) {
    ingestionProgress.value = value / 100;
  }
  function onStatus(text) { statusLabel.text = text; }
  function onStatus2(text) { massStatusLabel.text = text; }
  function onFinished() {
    ingestionProgress.visible = false;
    ingestionProgress.value = 0;
    addfilebtn.enabled = true;
    addfolderbtn.enabled = true;
  }
}
