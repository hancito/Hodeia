#pragma once
#include "ingestionpipeline.h"
#include "sqlitevec.h"
#include "summary.h"
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QThreadPool>
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
  /// Stop as soon as possible (app closing): no report.
  void stop();
  /// Stop at the next safe point at the user's request: whatever was not
  /// fully indexed is rolled back, and a report says what was imported.
  /// Thread-safe (atomics only): call it directly, not through the queue.
  void cancel();

signals:
  void finished();
  void started();
  void status(QString statusLog);
  /// Progress of one operation of the import, for the import window.
  /// `step` is "hash" (reading the selected files), "extract" (text
  /// extraction, counted in files) or "embed" (indexing the current batch,
  /// counted in passages).  `detail` describes the item in progress.
  void stepProgress(const QString &step, int current, int total,
                    const QString &detail);
  /// Papers processed (added or failed) out of those not yet in the library.
  void papersProgress(int done, int total);
  /// Emitted once, before finished(), with a user-facing summary of the
  /// import: papers added, files skipped, and what failed and why.  Not
  /// emitted when the import is stopped.
  void report(const QString &title, const QString &message, bool isError);

private:
  void emitCancelReport(int added);
  void emitReport(int selected, int alreadyKnown, int added,
                  const QStringList &unreadable, const QStringList &unindexed,
                  const QString &indexError);

  QMap<QString, QUrl> parallelHasher(const QList<QUrl> &fileList);
  /// Extraction threads that fit in the memory available now.
  static int extractThreadCount();

  /// How the import is going, kept by the indexer (processPDF's thread).
  struct ImportTally {
    int total = 0;  ///< papers to import (not in the library yet)
    int saved = 0;  ///< added to the library
    int errors = 0; ///< unreadable, or could not be indexed
    QStringList failedTitles; ///< papers dropped because indexing failed
    QStringList unreadable;   ///< "file — reason" for files that gave no text
    QString indexError;       ///< why indexing failed, when known
    int releasedAt = 0;       ///< done() at the last SystemMemory release
    int done() const { return saved + errors; }
  };
  /// Index a group of extracted papers given by the pipeline: skip the
  /// unreadable ones, embed and save the others, report the progress.
  void indexGroup(QList<ExtractionResult> &group, ImportTally &tally,
                  const QString &dbPath);
  /// Embed and save papers; if the embedding fails, try them one by one.
  void indexPapers(const QList<ExtractionResult> &papers, ImportTally &tally,
                   const QString &dbPath);

  /// What savePapers() stored.
  struct IndexResult {
    int saved = 0;
    QStringList failed; ///< titles of the papers that could not be stored
    QString error;      ///< the first database error, if any
  };
  IndexResult savePapers(const QList<ExtractionResult> &papers,
                         const QList<QByteArray> &embeddings,
                         const QString &dbPath);
  QStringList getAllPaperHashes(const QString &dbPath) const;
  /// The files of `hashesToProcess` (by hash) not in the library yet.
  QList<IngestionJob> newJobs(const QMap<QString, QUrl> &hashesToProcess,
                              const QString &dbPath) const;

  // Papers extracted (or being extracted) ahead of the indexer, per
  // extraction thread: the rest wait, unopened.
  static constexpr int kQueuePerThread = 2;
  // Chunks embedded together at most (the embedding model works in batches
  // of 64): small papers share a group, a large one is embedded alone.
  static constexpr int kGroupChunks = 256;
  // Papers indexed between two SystemMemory::releaseFreed().
  static constexpr int kReleaseEvery = 20;

  SqliteVecManager *m_vecManager = nullptr;
  std::atomic<bool> m_stopFlag{false};
  std::atomic<bool> m_userCancelled{false};

  // Progress counters for stepProgress().  m_extractDone is bumped by the
  // extraction threads; m_extractTotal is set before they start.
  std::atomic<int> m_extractDone{0}; // files whose extraction has finished
  int m_extractTotal = 0;            // files to extract in this import
  ImportTally *m_tally = nullptr; // during processPDF(), for the progress
  ConfigManager *m_config = nullptr;
  // Extraction threads, sized for the memory (see extractThreadCount()).
  QThreadPool m_extractPool;
};
