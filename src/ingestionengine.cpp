#include "ingestionengine.h"
#include "ingestionpipeline.h"
#include "libraryrepository.h"
#include "systemmemory.h"
#include "sqlitedb.h"
#include "pdfextractor.h"
#include "sqlitevec.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>
#include <QtConcurrent>

// ============================================================================
//  Lifecycle
// ============================================================================

PdfWorker::PdfWorker(ConfigManager *config, SqliteVecManager *vecManager,
                     QObject *parent)
    : QObject(parent), m_vecManager(vecManager), m_config(config) {
  if (m_vecManager) {
    // Emitted from this worker's thread (embedPassages): a direct call.
    // The indexing step counts papers; the passages of the group being
    // embedded go in its detail.
    connect(m_vecManager, &SqliteVecManager::embeddingProgress, this,
            [this](int current, int total) {
              if (m_tally)
                emit stepProgress(QStringLiteral("embed"), m_tally->done(),
                                  m_tally->total,
                                  tr("%1/%2 passages").arg(current).arg(total));
            });
  }
}

PdfWorker::~PdfWorker() = default;

void PdfWorker::stop() { m_stopFlag.store(true); }

void PdfWorker::cancel() {
  m_userCancelled.store(true);
  m_stopFlag.store(true);
}

// ============================================================================
//  Main ingestion pipeline
// ============================================================================

void PdfWorker::processPDF(const QList<QUrl> &filePath, const QString &imgPath,
                           const QString &dbPath) {
  m_stopFlag.store(false);
  m_userCancelled.store(false);
  QElapsedTimer importTimer; // for the log at the end
  importTimer.start();
  emit started();

  emit status(tr("Started with %1 files").arg(filePath.size()));

  emit stepProgress(QStringLiteral("hash"), 0, filePath.size(), QString());
  QMap<QString, QUrl> hashesToProcess = parallelHasher(filePath);
  if (m_stopFlag.load()) {
    if (m_userCancelled.load())
      emitCancelReport(0);
    emit finished();
    return;
  }
  emit stepProgress(QStringLiteral("hash"), filePath.size(), filePath.size(),
                    tr("Checking which papers are already in the library..."));
  const QList<IngestionJob> jobs = newJobs(hashesToProcess, dbPath);
  const int alreadyKnown = filePath.size() - jobs.size();
  emit status(tr("Skipped %1 papers").arg(alreadyKnown));
  emit stepProgress(QStringLiteral("hash"), filePath.size(), filePath.size(),
                    tr("%n paper(s) already in the library", "", alreadyKnown));

  // Load the embedding model here, once: the chunking of every extraction
  // thread asks it for token counts.
  if (m_vecManager)
    (void)m_vecManager->maxChunkTokens();

  // A continuous flow, paper by paper (IngestionPipeline): files are
  // extracted in parallel while the papers already extracted are embedded
  // and saved here.  Each paper is saved with its chunks in one transaction,
  // so it is either fully indexed or absent: a cancel keeps the papers
  // already saved, and a failure drops only the papers it hit.
  ImportTally tally;
  tally.total = int(jobs.size());
  m_tally = &tally;
  m_extractDone = 0;
  m_extractTotal = tally.total;
  emit papersProgress(0, tally.total);
  emit stepProgress(QStringLiteral("extract"), 0, tally.total, QString());
  emit stepProgress(QStringLiteral("embed"), 0, tally.total, QString());

  const PdfExtractor extractor(m_vecManager, &m_stopFlag);
  IngestionPipeline::Options options;
  options.threadCount = &PdfWorker::extractThreadCount;
  options.queuePerThread = kQueuePerThread;
  options.groupChunks = kGroupChunks;
  IngestionPipeline(&m_extractPool, options)
      .run(
          jobs,
          [this, &extractor, &imgPath](const IngestionJob &job) {
            if (m_stopFlag.load())
              return ExtractionResult{};
            ExtractionResult result = extractor.extract(
                job, imgPath,
                [this](const QString &fileName, int page, int pageCount) {
                  emit stepProgress(QStringLiteral("extract"),
                                    m_extractDone.load(), m_extractTotal,
                                    tr("%1 — page %2/%3")
                                        .arg(fileName)
                                        .arg(page)
                                        .arg(pageCount));
                });
            if (!m_stopFlag.load())
              emit stepProgress(QStringLiteral("extract"), ++m_extractDone,
                                m_extractTotal, result.fileName);
            return result;
          },
          [this, &tally, &dbPath](QList<ExtractionResult> &group) {
            indexGroup(group, tally, dbPath);
          },
          m_stopFlag);
  m_tally = nullptr;
  // Give what the extraction freed back to the system.
  SystemMemory::releaseFreed();

  if (tally.errors > 0)
    qWarning() << "[PdfWorker] Total skipped due to errors:" << tally.errors;
  const double seconds = importTimer.elapsed() / 1000.0;
  qInfo().noquote()
      << QString("[PdfWorker] Import done in %1 s: %2/%3 paper(s) added, %4 "
                 "failed, %5 already in the library (%6 papers/min)%7")
             .arg(seconds, 0, 'f', 1)
             .arg(tally.saved)
             .arg(tally.total)
             .arg(tally.errors)
             .arg(alreadyKnown)
             .arg(seconds > 0 ? tally.done() * 60.0 / seconds : 0.0, 0, 'f', 1)
             .arg(m_stopFlag.load() ? QStringLiteral(" — stopped") : QString());

  // No report when the app is closing (stop()), only for the user's cancel.
  // A cancel that came after the last paper changed nothing: normal report.
  const bool cancelled = m_stopFlag.load() && tally.done() < tally.total;
  if (cancelled) {
    emit status(tr("Ingestion cancelled"));
    if (m_userCancelled.load())
      emitCancelReport(tally.saved);
  } else if (!m_stopFlag.load() || m_userCancelled.load()) {
    emitReport(filePath.size(), alreadyKnown, tally.saved, tally.unreadable,
               tally.failedTitles, tally.indexError);
  }

  emit finished();
}

void PdfWorker::indexGroup(QList<ExtractionResult> &group, ImportTally &tally,
                           const QString &dbPath) {
  // The papers that can be indexed.
  QList<ExtractionResult> papers;
  for (auto &extraction : group) {
    // A file that could not be opened, or gave no text at all (not even
    // through OCR), can be neither summarized nor queried: don't add it.
    if (!extraction.error.isEmpty() || extraction.paperHash.isEmpty() ||
        extraction.title.isEmpty() || extraction.chunks.isEmpty()) {
      const QString reason = !extraction.error.isEmpty()
                                 ? extraction.error
                                 : tr("no text could be extracted");
      qCritical() << "[PdfWorker] Skipping file" << extraction.fileName << ":"
                  << reason;
      tally.unreadable << extraction.fileName + " — " + reason;
      ++tally.errors;
      continue;
    }

    // Propagate title as initial cleanTitle and citation on every chunk.
    // The summarizer will overwrite both with proper authors/year later.
    for (auto &tc : extraction.chunks) {
      tc.cleanTitle = extraction.title;
      tc.citation = extraction.title;
    }
    papers.append(std::move(extraction));
  }
  group.clear();

  indexPapers(papers, tally, dbPath);

  emit status(tr("Saved %1/%2 papers").arg(tally.done()).arg(tally.total));
  emit papersProgress(tally.done(), tally.total);
  emit stepProgress(QStringLiteral("embed"), tally.done(), tally.total,
                    QString());
  // Give what the extraction threads freed back to the system now and then
  // (the thread count follows the memory available).
  if (tally.done() - tally.releasedAt >= kReleaseEvery) {
    tally.releasedAt = tally.done();
    SystemMemory::releaseFreed();
  }
}

void PdfWorker::indexPapers(const QList<ExtractionResult> &papers,
                            ImportTally &tally, const QString &dbPath) {
  if (papers.isEmpty() || m_stopFlag.load())
    return;

  QList<TextChunk> chunks;
  for (const auto &paper : papers)
    chunks << paper.chunks;
  emit status(tr("Embedding %1 chunks...").arg(chunks.size()));

  // Outside any transaction: nothing is written until a paper's embeddings
  // are all there.
  QList<QByteArray> embeddings;
  if (m_vecManager)
    embeddings = m_vecManager->embedPassages(chunks, &m_stopFlag);
  else
    qWarning() << "[PdfWorker] embedding model not loaded, the papers cannot "
                  "be indexed";
  chunks.clear();

  if (embeddings.isEmpty()) {
    if (m_stopFlag.load())
      return; // cancelled: nothing of this group was saved
    // One paper the model could not read fails the whole group: try them one
    // by one, so that only that paper is dropped.
    if (papers.size() > 1 && m_vecManager && m_vecManager->isReady()) {
      for (const auto &paper : papers)
        indexPapers({paper}, tally, dbPath);
      return;
    }
    if (tally.indexError.isEmpty() && m_vecManager)
      tally.indexError = m_vecManager->lastError();
    tally.errors += papers.size();
    for (const auto &paper : papers)
      tally.failedTitles << paper.title;
    return;
  }

  const IndexResult r = savePapers(papers, embeddings, dbPath);
  tally.saved += r.saved;
  tally.errors += r.failed.size();
  tally.failedTitles << r.failed;
  if (tally.indexError.isEmpty())
    tally.indexError = r.error;
}

void PdfWorker::emitCancelReport(int added) {
  QString message =
      tr("%n paper(s) were added to the library before the import was "
         "cancelled.",
         nullptr, added);
  message += "\n\n" + tr("To import the remaining files, add them again: "
                          "the papers already in the library will be "
                          "skipped.");
  emit report(tr("Import Cancelled"), message, false);
}

void PdfWorker::emitReport(int selected, int alreadyKnown, int added,
                           const QStringList &unreadable,
                           const QStringList &unindexed,
                           const QString &indexError) {
  if (selected == 0) {
    emit report(tr("Nothing to Import"),
                tr("No PDF file was found in the selection."), false);
    return;
  }

  QStringList parts;
  parts << tr("%n paper(s) added to the library.", nullptr, added);
  if (alreadyKnown > 0)
    parts << tr("%n file(s) skipped: already in the library.", nullptr,
                alreadyKnown);
  if (!unreadable.isEmpty())
    parts << tr("%n file(s) could not be read:", nullptr,
                int(unreadable.size())) +
                 "\n• " + unreadable.join("\n• ");
  if (!unindexed.isEmpty()) {
    QString cause = indexError.isEmpty()
                        ? tr("The embedding model could not process their "
                             "text. See the log for details.")
                        : indexError;
    parts << tr("%n paper(s) could not be indexed for search and were not "
                "added:",
                nullptr, int(unindexed.size())) +
                 "\n• " + unindexed.join("\n• ") + "\n\n" + cause + "\n" +
                 tr("Add the files again once the problem is fixed.");
  }

  const bool failed = !unreadable.isEmpty() || !unindexed.isEmpty();
  QString title = tr("Import Complete");
  if (failed)
    title = added > 0 ? tr("Import Partly Failed") : tr("Import Failed");
  emit report(title, parts.join("\n\n"), failed);
}

// ============================================================================
//  Database writes
// ============================================================================

/// Store each paper with its chunks in its own transaction, in the order of
/// `papers`; `embeddings` holds those chunks' embeddings in the same order.
/// Stops between two papers once m_stopFlag is set: the papers stored so far
/// stay in the library.
PdfWorker::IndexResult
PdfWorker::savePapers(const QList<ExtractionResult> &papers,
                      const QList<QByteArray> &embeddings,
                      const QString &dbPath) {
  IndexResult result;
  SqliteDb db;
  if (auto r = db.open(dbPath); !r) {
    qCritical() << "[PdfWorker] Failed to open the database to save the papers:"
                << r.error();
    result.error = r.error();
    for (const auto &paper : papers)
      result.failed << paper.title;
    return result;
  }

  LibraryRepository repo(db);
  QElapsedTimer timer; // for the log at the end
  timer.start();
  qsizetype offset = 0;
  for (const auto &paper : papers) {
    if (m_stopFlag.load())
      break;
    SummaryRow row;
    row.paperHash = paper.paperHash;
    row.title = paper.title;
    row.pagesNum = paper.pageCount;
    row.date = QDateTime::currentDateTimeUtc();
    const qsizetype n = paper.chunks.size();
    if (auto r = repo.addIndexedPaper(row, paper.chunks,
                                      embeddings.mid(offset, n));
        r) {
      ++result.saved;
    } else {
      qCritical() << "[PdfWorker] Saving" << paper.title
                  << "failed:" << r.error();
      result.failed << paper.title;
      if (result.error.isEmpty())
        result.error = r.error();
    }
    offset += n;
  }
  qInfo().noquote() << QString("[PdfWorker] Saved %1 paper(s) in %2 ms")
                           .arg(result.saved)
                           .arg(timer.elapsed());

  if (auto r = db.exec("PRAGMA wal_checkpoint(TRUNCATE)"); !r)
    qCritical() << "[PdfWorker] WAL checkpoint failed:" << r.error();
  return result;
}

// ============================================================================
//  Hash helpers
// ============================================================================

QStringList PdfWorker::getAllPaperHashes(const QString &dbPath) const {
  SqliteDb db;
  auto openResult = db.open(dbPath);
  if (!openResult) {
    qCritical() << "[PdfWorker] Failed to open database for reading:"
                << openResult.error();
    return {};
  }
  auto hashes = LibraryRepository(db).paperHashes();
  if (!hashes) {
    qCritical() << "[PdfWorker] Error reading paper hashes:" << hashes.error();
    return {};
  }
  return *hashes;
}

QMap<QString, QUrl>
PdfWorker::parallelHasher(const QList<QUrl> &fileList) {
  QMap<QString, QUrl> hashToUrl;

  // Pool threads emit stepProgress: queued to the controller's thread.
  std::atomic<int> hashed{0};
  const int total = fileList.size();
  QList<QPair<QString, QUrl>> hashUrlPairs = QtConcurrent::blockingMapped(
      fileList, [this, &hashed, total](const QUrl &url) {
        // Once stopped, skip the remaining files: the caller checks the flag.
        if (m_stopFlag.load())
          return qMakePair(QString(), url);
        auto pair = qMakePair(PdfExtractor::fileHash(url), url);
        emit stepProgress(QStringLiteral("hash"), ++hashed, total,
                          QFileInfo(url.toLocalFile()).fileName());
        return pair;
      });

  for (const auto &pair : hashUrlPairs) {
    hashToUrl.insert(pair.first, pair.second);
  }

  return hashToUrl;
}

QList<IngestionJob>
PdfWorker::newJobs(const QMap<QString, QUrl> &hashesToProcess,
                   const QString &dbPath) const {
  const QStringList known = getAllPaperHashes(dbPath);
  const QSet<QString> existing(known.cbegin(), known.cend());
  QList<IngestionJob> jobs;
  for (auto it = hashesToProcess.constBegin(); it != hashesToProcess.constEnd();
       ++it)
    if (!existing.contains(it.key()))
      jobs.append({it.value(), it.key()});
  return jobs;
}

// ============================================================================
//  Parallel dispatch
// ============================================================================

/// Memory one extraction thread may take at its peak: a MuPDF context (cache
/// capped at kMuPdfStoreBytes), a Tesseract instance (~100 MB with eng+fra,
/// single-threaded) and the page images being made (200 DPI RGB and 300 DPI
/// gray for OCR).  ~45 MB measured on text PDFs; OCR adds ~110 MB.
static constexpr qint64 kExtractBytesPerThread = qint64(256) << 20;
/// Share of the available memory the extraction may take: the rest stays
/// for the embedding model, a local LLM and the other applications.
static constexpr double kExtractMemoryShare = 0.5;

/// Files extracted at once: as many as fit in kExtractMemoryShare of the
/// memory available now, at most half the logical cores (OCR and rendering
/// are CPU-bound; hyper-threads add little).  Memory unknown: 2.
int PdfWorker::extractThreadCount() {
  return SystemMemory::tasksThatFit(SystemMemory::availableBytes(),
                                    kExtractBytesPerThread, kExtractMemoryShare,
                                    QThread::idealThreadCount() / 2,
                                    /*fallback=*/2);
}
