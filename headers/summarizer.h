#pragma once

#include <QObject>
#include <QStringList>

class ConfigManager;
class DatabaseManager;
class BatchWorker;
class LocalBackend;
class RemoteBackend;
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

  /// Check the remote backend connection and retrieve available models.
  /// For remote backends: calls fetchModels(apiUrl, apiKey) on the remote
  /// backend.  For local backends: scans the Summarizers folder for .gguf
  /// files.  On success the backend is saved to ConfigManager (setBackend +
  /// setActiveBackend).  On failure an error is reported.
  /// Asynchronous: result reported via the modelsChecked signal.
  Q_INVOKABLE void checkModels(const QString &name, const QString &apiUrl,
                               const QString &apiKey);

  /// Load available models for the currently active backend (local or remote).
  /// Emits modelsChecked() with the list.
  Q_INVOKABLE void loadActiveBackendModels();

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

  /// Remote backend check result.
  /// @param success  true if the API responded.
  /// @param models   List of model IDs (empty on failure).
  /// @param error    Error description (empty on success).
  void modelsChecked(bool success, const QStringList &models,
                     const QString &error);

private:
  /// Scan the Summarizers folder and return model names (without .gguf
  /// extension).
  QStringList listLocalModels() const;

  ConfigManager *m_config = nullptr;
  DatabaseManager *m_database = nullptr;
  BatchWorker *m_worker = nullptr;

  LocalBackend *m_localBackend = nullptr;
  RemoteBackend *m_remoteBackend = nullptr;

  // Worker thread for model fetching
  QThread *m_workerThread = nullptr;

  bool m_isBusy = false;
};
