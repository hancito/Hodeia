#pragma once
#include "headers/sqlitevec.h"
#include "headers/summary.h"
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QUrl>
#include <atomic>

class PdfWorker : public QObject {
  Q_OBJECT
public:
  explicit PdfWorker(ConfigManager *config, SqliteVecManager *vecManager,
                     QObject *parent = nullptr);
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
  ExtractionResult extractText(const QUrl &fileUrl,
                               const QString &imgPath) const;
  QString cleanPdfText(const QString &raw) const;
  QList<TextChunk> chunkDocumentText(const QString &fullText,
                                     const QVector<quint16> &pageMap,
                                     const QString &paperHash) const;
  QString computeFileHash(const QUrl &fileUrl) const;

  QMap<QString, QUrl> parallelHasher(const QList<QUrl> &fileList) const;
  QList<ExtractionResult> parallelExtract(const QList<QUrl> &filePaths,
                                          const QString &imgPath) const;
  void savePapers(const QList<SummaryRow> &rows, const QString &dbPath);
  void ingestChunks(const QList<TextChunk> &chunks, const QString &dbPath);
  QStringList getAllPaperHashes(const QString &dbPath) const;
  QList<QUrl> filterExistingHashes(const QMap<QString, QUrl> &hashesToProcess,
                                   const QString &dbPath) const;

  // Chunking parameters
  int m_maxChunkChars = 1500;
  int m_overlapChars = 200;

  SqliteVecManager *m_vecManager = nullptr;
  std::atomic<bool> m_stopFlag{false};
  ConfigManager *m_config = nullptr;
};
