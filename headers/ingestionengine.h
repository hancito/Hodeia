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
