#pragma once

#include "summary.h"

#include <QList>
#include <QString>
#include <QUrl>
#include <atomic>
#include <functional>

class QThreadPool;

/// A file to import, with its hash (computed once, before the import).
struct IngestionJob {
  QUrl url;
  QString paperHash;
};

/// Runs an import as a continuous flow, paper by paper:
///
///   files ──► extraction pool ──► bounded queue ──► indexer (caller thread)
///
/// Each file is a task of its own: a thread that finishes a file takes the
/// next one, so a long file (a scan, a book) holds one thread while the
/// others go on.  Extracted papers wait in a queue for the indexer, which
/// takes whatever is ready (grouped up to `groupChunks` chunks, for the
/// embedding model).  The queue is bounded: when the indexer falls behind,
/// no new file is started, so extracted text does not pile up in memory.
///
/// No MuPDF, no SQL: the work is done by the two functions given to run(),
/// which makes the flow testable with fake ones.
class IngestionPipeline {
public:
  /// Extract one file.  Runs on a pool thread; must return soon once the
  /// stop flag is set (its result is then dropped).
  using ExtractFn = std::function<ExtractionResult(const IngestionJob &)>;
  /// Index a group of extracted papers (failed extractions included: the
  /// function decides what to do with them).  Runs on the thread of run().
  using IndexFn = std::function<void(QList<ExtractionResult> &group)>;

  struct Options {
    /// Extraction threads that fit now (memory, cores): asked again before
    /// each new file is started.  At least 1 is used.
    std::function<int()> threadCount = [] { return 2; };
    /// Papers extracted or being extracted, not yet indexed, per thread.
    int queuePerThread = 2;
    /// Chunks indexed together at most.  A paper is never split: one with
    /// more chunks is indexed alone.
    int groupChunks = 256;
  };

  IngestionPipeline(QThreadPool *pool, Options options);

  /// Extract and index `jobs`; returns once every started extraction has
  /// ended.  Papers are indexed in the order their extraction ends.  Once
  /// `stop` is set, no file is started, and the papers not yet given to
  /// `index` are dropped; those already indexed stay.
  void run(const QList<IngestionJob> &jobs, const ExtractFn &extract,
           const IndexFn &index, const std::atomic<bool> &stop);

private:
  QThreadPool *m_pool;
  Options m_options;
};
