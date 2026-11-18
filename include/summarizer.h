#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantMap>

#include "chatbackend.h"
#include "localprompt.h"
#include "summaryformat.h"

#include <memory>

class ConfigManager;
class DatabaseManager;

/// Orchestrator for paper summarization.
class Summarizer : public QObject {
  Q_OBJECT

public:
  explicit Summarizer(ConfigManager *config, DatabaseManager *database,
                      QObject *parent = nullptr);
  ~Summarizer() override;

public slots:
  void summarizePapers(const QStringList &hashes);
  void stop();

signals:
  /// @p title is the paper's title (for user-facing messages); @p message
  /// says what happened, including the cause on failure.
  void paperFinished(const QString &hash, const QString &title, bool success,
                     const QString &message);
  void allFinished(int successCount, int total);
  void started();
  void stopped();

private:
  /* GPU lifecycle */
  /// Unload the embedding model when the backend needs the GPU; true if it
  /// was loaded (cleanupGpu() then reloads it).
  bool prepareGpu();
  void cleanupGpu(bool wasEmbeddingReady);

  /* inference */
  /// The generated text (empty if cancelled), or a user-facing error.
  std::expected<QString, QString> generate(const PromptPayload &payload,
                                           const ActiveBackend &active);

  /* prompt building */
  /// Read the paper's chunks and build its prompt (SummaryPrompt).
  PromptPayload buildPrompt(const QString &paperHash) const;

  /* result handling */
  std::expected<void, QString> saveSummary(const QString &hash,
                                           const PaperSummary &summary,
                                           const QString &rawJson,
                                           const QString &modelName);
  /// Title stored at ingestion, or the hash if it cannot be read.
  QString paperTitle(const QString &hash) const;

  bool isStopping() const { return m_stopping; }

  bool m_stopping = false;
  // Tokens reserved for the generated response when sizing the local model's
  // KV cache (prompt_tokens + m_headroom). Summaries are compact, so this is
  // smaller than the RAG query headroom.
  uint32_t m_headroom = 2048;
  // Set by stop() to cooperatively cancel the in-flight local generation
  // (polled by InferenceEngine::generate()). A cancelled paper is not saved.
  bool m_cancelled = false;
  ConfigManager *m_config = nullptr;
  DatabaseManager *m_database = nullptr;
  /// Backend of the running batch (null between batches).
  std::unique_ptr<ChatBackend> m_backend;
};
