#pragma once
#include "sqlitevec.h"
#include "summary.h"
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QUrl>
#include <atomic>

extern "C" {
#include <mupdf/fitz.h>
}

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
  /// Emitted once, before finished(), with a user-facing summary of the
  /// import: papers added, files skipped, and what failed and why.  Not
  /// emitted when the import is stopped.
  void report(const QString &title, const QString &message, bool isError);

private:
  ExtractionResult extractText(const QUrl &fileUrl,
                               const QString &imgPath) const;
  QString extractTextFromStextPage(fz_stext_page *textPage) const;
  QString extractTextWithOcr(fz_context *ctx, fz_document *doc,
                             int pageNum) const;
  QList<TextChunk>
  chunkDocumentText(const QString &fullText, const QVector<quint16> &pageMap,
                    const QString &paperHash,
                    const QVector<PageMetadata> &pageMeta) const;
  QString computeFileHash(const QUrl &fileUrl) const;
  void emitReport(int selected, int alreadyKnown, int added,
                  const QStringList &unreadable, const QStringList &unindexed,
                  const QString &indexError);

  QMap<QString, QUrl> parallelHasher(const QList<QUrl> &fileList) const;
  QList<ExtractionResult> parallelExtract(const QList<QUrl> &filePaths,
                                          const QString &imgPath) const;
  void savePapers(const QList<SummaryRow> &rows, const QString &dbPath);
  bool ingestChunks(const QList<TextChunk> &chunks, const QString &dbPath);
  void removePapers(const QList<SummaryRow> &rows, const QString &dbPath);
  QStringList getAllPaperHashes(const QString &dbPath) const;
  QList<QUrl> filterExistingHashes(const QMap<QString, QUrl> &hashesToProcess,
                                   const QString &dbPath) const;

  // Chunk size limit (tokens) when the embedding model can't be loaded to
  // provide its own; otherwise chunks follow SqliteVecManager::maxChunkTokens.
  static constexpr int FallbackMaxChunkTokens = 480;

  SqliteVecManager *m_vecManager = nullptr;
  std::atomic<bool> m_stopFlag{false};
  ConfigManager *m_config = nullptr;
};
