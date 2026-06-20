#pragma once
#include "headers/summary.h"
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QUrl>

class PdfWorker : public QObject {
  Q_OBJECT
public:
  explicit PdfWorker(QObject *parent = nullptr);
  ~PdfWorker();

public slots:
  void processPDF(const QList<QUrl> &filePath, const QString &imgPath,
                  const QString &dbPath);
  void stop();

signals:
  void finished();
  void started();
  void status(QString statusLog);

private:
  ExtractionResult extractText(const QUrl &filePath,
                               const QString &imgPath) const;

  QMap<QString, QUrl> parallelHasher(const QList<QUrl> &fileList) const;
  QList<ExtractionResult> parallelExtract(const QList<QUrl> &filePaths,
                                          const QString &imgPath) const;
  void savePapers(const QList<SummaryRow> &rows, const QString &dbPath);
  QStringList getAllPaperHashes(const QString &dbPath) const;
  QList<QUrl> filterExistingHashes(const QMap<QString, QUrl> &hashesToProcess,
                                   const QString &dbPath) const;
};
