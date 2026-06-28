#pragma once
#include "headers/sqlitevec.h"
#include "headers/summary.h"
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QUrl>

class PdfWorker : public QObject {
  Q_OBJECT
public:
  explicit PdfWorker(ConfigManager *config, QObject *parent = nullptr);
  ~PdfWorker();

public slots:
  void processPDF(const QList<QUrl> &filePath, const QString &imgPath,
                  const QString &dbPath);
  void stop();

signals:
  void finished();
  void started();
  void status(QString statusLog);
  void embeddingProgress(int current, int total);

private:
  ExtractionResult extractText(const QUrl &filePath,
                               const QString &imgPath) const;
  QList<TextChunk> chunkPageText(const QString &pageText,
                                 const QString &paperHash, int pageNum) const;

  QMap<QString, QUrl> parallelHasher(const QList<QUrl> &fileList) const;
  QList<ExtractionResult> parallelExtract(const QList<QUrl> &filePaths,
                                          const QString &imgPath) const;
  void savePapers(const QList<SummaryRow> &rows, const QString &dbPath);
  void ingestChunks(const QList<TextChunk> &chunks, const QString &dbPath);
  QStringList getAllPaperHashes(const QString &dbPath) const;
  QList<QUrl> filterExistingHashes(const QMap<QString, QUrl> &hashesToProcess,
                                   const QString &dbPath) const;

  mutable SqliteVecManager m_vecManager;
  bool m_stopFlag = false;
  ConfigManager *m_config = nullptr;
};
