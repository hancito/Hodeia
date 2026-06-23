#pragma once

#include <QObject>
#include <QStringList>

class ConfigManager;
class DatabaseManager;
class LlamaCppBackend;
class OpenAiServerBackend;
class QThread;

/// Paper-level summary produced by the LLM, mirrors the Python PaperSummary
/// schema.
struct PaperSummary {
  QString title;
  QStringList authors;
  QString journal;
  int year = 0;
  QStringList topics;
  QString researchObjective;
  QString methodology;
  QString coreFindingsAndSignificance;
  QString criticalLimitations;
  QString citableSummary;
};

/// Central orchestrator for paper summarization.
///
/// The heavy lifting (chunk retrieval, LLM calls, DB writes) runs in a
/// dedicated worker thread so the main thread stays responsive.
///
/// Usage from QML:
///   summarizer.summarizePapers(["hash1", "hash2"])
class Summarizer : public QObject {
  Q_OBJECT

  Q_PROPERTY(bool isBusy READ isBusy NOTIFY busyChanged)

public:
  explicit Summarizer(ConfigManager *config, DatabaseManager *database,
                      QObject *parent = nullptr);
  ~Summarizer() override;

  bool isBusy() const { return m_isBusy; }

  /// Summarize the papers identified by the given MD5 hashes.
  /// Asynchronous: results are reported via signals.
  Q_INVOKABLE void summarizePapers(const QStringList &hashes);

  /// Cancel an in-progress summarization batch.
  Q_INVOKABLE void cancel();

  /// Get the system prompt template sent to the LLM.
  static const QString &systemPrompt();

signals:
  void busyChanged();

  /// Per-paper progress: hash, stage description.
  void progressUpdated(const QString &hash, const QString &status);

  /// Per-paper result: hash, success flag, short message (or error).
  void paperFinished(const QString &hash, bool success, const QString &message);

  /// Entire batch completed.
  void allFinished(int successCount, int total);

private:
  // ---- called from the worker thread ----
  void runBatch();

  /// Retrieve concatenated text context for a paper from chunks table.
  QString retrieveContext(const QString &paperHash) const;

  /// Build the user prompt from reconstructed text.
  QString buildUserPrompt(const QString &textContext) const;

  /// Parse raw LLM output into a structured PaperSummary.
  PaperSummary parseSummary(const QString &rawOutput) const;

  /// Convert PaperSummary to a JSON QByteArray.
  QByteArray summaryToJson(const PaperSummary &s) const;

  /// Upsert summary row into the Summaries table.
  bool upsertSummary(const QString &paperHash, const QByteArray &jsonBytes,
                     const QString &modelName);

  ConfigManager *m_config = nullptr;
  DatabaseManager *m_database = nullptr;

  LlamaCppBackend *m_localBackend = nullptr;
  OpenAiServerBackend *m_remoteBackend = nullptr;

  // Worker thread
  QThread *m_workerThread = nullptr;

  // Batch state (set from main thread, consumed in worker)
  QStringList m_hashes;
  bool m_isBusy = false;
  bool m_cancelled = false;
};
