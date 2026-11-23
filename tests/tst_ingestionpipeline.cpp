// Unit tests for IngestionPipeline, with fake extraction and indexing
// functions: no PDF, no database, no embedding model.

#include "ingestionpipeline.h"

#include <QSemaphore>
#include <QThread>
#include <QThreadPool>
#include <QtTest>
#include <atomic>

namespace {

QList<IngestionJob> jobs(int n) {
  QList<IngestionJob> list;
  for (int i = 0; i < n; ++i)
    list.append({QUrl::fromLocalFile(QString("/f%1.pdf").arg(i)),
                 QString("h%1").arg(i)});
  return list;
}

/// An extracted paper with `chunks` chunks.
ExtractionResult paper(const IngestionJob &job, int chunks = 1) {
  ExtractionResult r;
  r.paperHash = job.paperHash;
  r.title = job.paperHash;
  r.fileName = job.url.fileName();
  for (int i = 0; i < chunks; ++i)
    r.chunks.append(TextChunk{"text", job.paperHash, 0, i, {}, {}, {}});
  return r;
}

/// Hashes of the papers indexed, in order (index runs on the test thread).
struct Indexed {
  QStringList hashes;
  QList<int> groupSizes;
  void add(const QList<ExtractionResult> &group) {
    groupSizes.append(int(group.size()));
    for (const auto &r : group)
      hashes.append(r.paperHash);
  }
};

IngestionPipeline::Options options(int threads, int queuePerThread = 2,
                                   int groupChunks = 256) {
  IngestionPipeline::Options o;
  o.threadCount = [threads] { return threads; };
  o.queuePerThread = queuePerThread;
  o.groupChunks = groupChunks;
  return o;
}

} // namespace

class TestIngestionPipeline : public QObject {
  Q_OBJECT

private slots:
  void indexesEveryPaperOnce();
  void nothingToDo();
  void slowFileDoesNotBlockTheOthers();
  void fullQueuePausesTheExtraction();
  void stopKeepsWhatIsIndexed();
  void groupsFollowChunkLimit();
  void threadCountIsFollowed();

private:
  QThreadPool m_pool;
};

void TestIngestionPipeline::indexesEveryPaperOnce() {
  const auto list = jobs(25);
  Indexed indexed;
  std::atomic<bool> stop{false};
  IngestionPipeline(&m_pool, options(4))
      .run(list, [](const IngestionJob &j) { return paper(j); },
           [&](QList<ExtractionResult> &g) { indexed.add(g); }, stop);
  QCOMPARE(indexed.hashes.size(), 25);
  QStringList sorted = indexed.hashes;
  sorted.sort();
  sorted.removeDuplicates();
  QCOMPARE(sorted.size(), 25);
}

void TestIngestionPipeline::nothingToDo() {
  int calls = 0;
  std::atomic<bool> stop{false};
  IngestionPipeline(&m_pool, options(2))
      .run({}, [](const IngestionJob &j) { return paper(j); },
           [&](QList<ExtractionResult> &) { ++calls; }, stop);
  QCOMPARE(calls, 0);
}

// The slow file waits until every other one is indexed: it would deadlock
// (and time out) if it held up the others.
void TestIngestionPipeline::slowFileDoesNotBlockTheOthers() {
  const auto list = jobs(10);
  QSemaphore othersIndexed;
  std::atomic<bool> slowTimedOut{false};
  Indexed indexed;
  std::atomic<bool> stop{false};
  IngestionPipeline(&m_pool, options(2))
      .run(list,
           [&](const IngestionJob &j) {
             if (j.paperHash == "h0" && !othersIndexed.tryAcquire(1, 5000))
               slowTimedOut = true;
             return paper(j);
           },
           [&](QList<ExtractionResult> &g) {
             indexed.add(g);
             if (indexed.hashes.size() == 9 && !indexed.hashes.contains("h0"))
               othersIndexed.release();
           },
           stop);
  QVERIFY(!slowTimedOut);
  QCOMPARE(indexed.hashes.size(), 10);
  QCOMPARE(indexed.hashes.last(), QString("h0"));
}

// A slow indexer: never more papers extracted (or being extracted) and not
// yet indexed than the queue holds, plus the group being indexed.
void TestIngestionPipeline::fullQueuePausesTheExtraction() {
  const auto list = jobs(30);
  std::atomic<int> started{0};
  std::atomic<int> done{0};
  std::atomic<int> maxAhead{0};
  std::atomic<bool> stop{false};
  IngestionPipeline(&m_pool, options(2, 2, /*groupChunks=*/1))
      .run(list,
           [&](const IngestionJob &j) {
             const int ahead = ++started - done.load();
             int seen = maxAhead.load();
             while (ahead > seen && !maxAhead.compare_exchange_weak(seen, ahead)) {
             }
             return paper(j);
           },
           [&](QList<ExtractionResult> &g) {
             QThread::msleep(20);
             done += int(g.size());
           },
           stop);
  QCOMPARE(done.load(), 30);
  // Capacity 2 × 2 = 4, plus the paper being indexed.
  QVERIFY2(maxAhead.load() <= 5, qPrintable(QString::number(maxAhead.load())));
}

void TestIngestionPipeline::stopKeepsWhatIsIndexed() {
  const auto list = jobs(40);
  std::atomic<bool> stop{false};
  std::atomic<int> started{0};
  Indexed indexed;
  IngestionPipeline(&m_pool, options(3, 2, 1))
      .run(list,
           [&](const IngestionJob &j) {
             ++started;
             QThread::msleep(5);
             return paper(j);
           },
           [&](QList<ExtractionResult> &g) {
             indexed.add(g);
             if (indexed.hashes.size() == 3)
               stop = true;
           },
           stop);
  // Nothing indexed after the stop, and no file started after it: at most
  // the 3 indexed and the 3 × 2 the queue could hold then.
  QCOMPARE(indexed.hashes.size(), 3);
  QVERIFY2(started.load() <= 9, qPrintable(QString::number(started.load())));
}

void TestIngestionPipeline::groupsFollowChunkLimit() {
  // Papers of 3 chunks, groups of at most 10 chunks: 3 papers a group at
  // most; a paper of 50 chunks is indexed alone.
  QList<IngestionJob> list = jobs(12);
  std::atomic<bool> stop{false};
  QList<qsizetype> groupChunks;
  QStringList big;
  IngestionPipeline(&m_pool, options(4, 4, 10))
      .run(list,
           [](const IngestionJob &j) {
             return paper(j, j.paperHash == "h5" ? 50 : 3);
           },
           [&](QList<ExtractionResult> &g) {
             qsizetype n = 0;
             for (const auto &r : g)
               n += r.chunks.size();
             groupChunks.append(n);
             if (n == 50)
               big.append(g.first().paperHash);
             QThread::msleep(10); // let papers pile up
           },
           stop);
  qsizetype total = 0;
  for (qsizetype n : groupChunks) {
    QVERIFY(n == 50 || n <= 10);
    total += n;
  }
  QCOMPARE(total, qsizetype(11 * 3 + 50));
  QCOMPARE(big, QStringList({"h5"}));
}

// The thread count is asked again as the import goes, and never exceeded.
void TestIngestionPipeline::threadCountIsFollowed() {
  for (int threads : {1, 3}) {
    std::atomic<int> asked{0};
    std::atomic<int> running{0};
    std::atomic<int> maxRunning{0};
    std::atomic<bool> stop{false};
    IngestionPipeline::Options o = options(threads);
    o.threadCount = [&] {
      ++asked;
      return threads;
    };
    IngestionPipeline(&m_pool, o)
        .run(jobs(20),
             [&](const IngestionJob &j) {
               const int now = ++running;
               int seen = maxRunning.load();
               while (now > seen &&
                      !maxRunning.compare_exchange_weak(seen, now)) {
               }
               QThread::msleep(10);
               --running;
               return paper(j);
             },
             [](QList<ExtractionResult> &) {}, stop);
    QVERIFY(asked.load() > 1);
    QVERIFY(maxRunning.load() <= threads);
    if (threads > 1)
      QVERIFY(maxRunning.load() > 1); // the files do run side by side
  }
}

QTEST_GUILESS_MAIN(TestIngestionPipeline)
#include "tst_ingestionpipeline.moc"
