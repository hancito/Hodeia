#include "ingestionpipeline.h"

#include <QThreadPool>
#include <condition_variable>
#include <mutex>

IngestionPipeline::IngestionPipeline(QThreadPool *pool, Options options)
    : m_pool(pool), m_options(std::move(options)) {}

void IngestionPipeline::run(const QList<IngestionJob> &jobs,
                            const ExtractFn &extract, const IndexFn &index,
                            const std::atomic<bool> &stop) {
  // All shared state is guarded by `mutex`.  Files are started both from
  // here (when the indexer frees room in the queue) and from the pool
  // threads (when a file ends): the extraction goes on while the indexer
  // works.  run() returns only once inFlight is 0, so the tasks never
  // outlive what they reference.
  std::mutex mutex;
  std::condition_variable wake;
  QList<ExtractionResult> ready; // extracted, waiting for the indexer
  int inFlight = 0;              // files being extracted
  qsizetype next = 0;            // next job to start

  std::function<void()> fill; // start files while there is room
  fill = [&] {
    if (stop.load() || next >= jobs.size())
      return;
    const int threads = qMax(1, m_options.threadCount());
    const qsizetype capacity = qsizetype(threads) * qMax(1, m_options.queuePerThread);
    m_pool->setMaxThreadCount(threads);
    while (next < jobs.size() && inFlight < threads &&
           inFlight + ready.size() < capacity) {
      ++inFlight;
      const IngestionJob job = jobs[next++];
      m_pool->start([&, job] {
        ExtractionResult result = extract(job);
        std::lock_guard lock(mutex);
        ready.append(std::move(result));
        --inFlight;
        fill();
        // Under the lock: once it is released with inFlight at 0, run()
        // may return and `wake` be gone.
        wake.notify_one();
      });
    }
  };

  std::unique_lock lock(mutex);
  fill();
  while (true) {
    if (stop.load())
      ready.clear(); // not indexed: dropped
    if (ready.isEmpty()) {
      if (inFlight == 0 && (stop.load() || next >= jobs.size()))
        break;
      wake.wait(lock);
      continue;
    }

    // Whatever is ready, up to groupChunks chunks (at least one paper).
    QList<ExtractionResult> group;
    qsizetype chunks = 0;
    while (!ready.isEmpty() &&
           (group.isEmpty() ||
            chunks + ready.first().chunks.size() <= m_options.groupChunks)) {
      chunks += ready.first().chunks.size();
      group.append(ready.takeFirst());
    }
    fill(); // the queue has room again

    lock.unlock();
    index(group);
    lock.lock();
  }
}
