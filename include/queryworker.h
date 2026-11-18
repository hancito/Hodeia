#pragma once

#include <QObject>
#include <expected>
#include <memory>

#include "chatbackend.h"
#include "localprompt.h"

class ConfigManager;
class DatabaseManager;

/// Orchestrator for RAG queries over the paper library: Retriever (pages),
/// QueryPrompt (prompt), ChatBackend (answer), CitationFormat (citations and
/// Sources footer), then the answer is saved in the query history.
class QueryWorker : public QObject {
  Q_OBJECT

public:
  explicit QueryWorker(ConfigManager *config, DatabaseManager *database,
                       QObject *parent = nullptr);
  ~QueryWorker() override;

  /// Tokens reserved for the generated response when sizing the local
  /// model's KV cache (prompt_tokens + headroom).  RAG answers synthesize
  /// across multiple papers with citations, so this is larger than the
  /// summary headroom.
  static constexpr uint32_t ResponseHeadroom = 4096;
  /// Abort a remote request with no answer after this long.  Generous: a
  /// large multimodal prompt can take minutes to process.
  static constexpr int RemoteTimeoutMs = 10 * 60 * 1000;

public slots:
  /// Execute a RAG query.
  /// \param queryText      the user's natural-language question
  /// \param contextTokens  prompt budget in tokens; 0 = auto (largest that
  ///                       runs fully on the GPU for a local model)
  void executeQuery(const QString &queryText, int contextTokens);
  void stop();

signals:
  /// On failure @p error is the cause; on success it is empty, or a warning
  /// when the answer could not be saved in the history.
  void queryFinished(const QString &response, bool success,
                     const QString &error);
  void started();
  void stopped();

private:
  /* backend lifecycle */
  /// Free the backend (and a local model's memory) once the query is over.
  void releaseBackend();

  /* inference — the generated text, or a user-facing error message */
  std::expected<QString, QString> generate(const PromptPayload &payload,
                                           const ActiveBackend &active);

  /* result handling */
  std::expected<void, QString> saveQuery(const QString &queryText,
                                         const QString &response,
                                         const QString &modelName);

  bool isStopping() const { return m_stopping; }

  // Called at the start of each executeQuery so a cancellation from a
  // previous, abandoned run cannot bleed into the next one.
  void resetCancellation() { m_cancelled = false; }

  bool m_stopping = false;
  bool m_cancelled = false;
  ConfigManager *m_config = nullptr;
  DatabaseManager *m_database = nullptr;
  /// Backend of the running query (null between queries).
  std::unique_ptr<ChatBackend> m_backend;
};
