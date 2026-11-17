#pragma once

#include <QObject>
#include <QSet>
#include <QVariantMap>
#include <expected>
#include <memory>

#include "chatbackend.h"
#include "localprompt.h"

class ConfigManager;
class DatabaseManager;

/// One page worth of context aggregated from vector search results.
struct PageContext {
  QString paperHash;
  int page;
  QString cleanTitle;
  QString citation;
  QStringList textChunks; ///< may hold multiple chunks from the same page
  QString imageFile;      ///< absolute PNG path, empty if unavailable
  bool withImage = false; ///< image sent to the model (visual mode, within
                          ///< the image cap and the token budget)
};

/// Orchestrator for RAG queries over the paper library.
/// Retrieves relevant chunks via vector search, groups by page,
/// builds a structured prompt, runs inference (local or remote),
/// and persists the response with a sources footer.
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
  /// Prompt budget used for remote backends in "auto" mode (their context
  /// size is unknown to the app).
  static constexpr int RemoteAutoTokens = 16384;
  /// Upper bound offered for remote backends.
  static constexpr int RemoteMaxTokens = 262144;
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
  /* GPU lifecycle */
  bool prepareGpu(bool isLocal);
  void cleanupGpu(bool isLocal, bool wasEmbeddingReady);

  /* inference — the generated text, or a user-facing error message */
  std::expected<QString, QString> generate(const PromptPayload &payload,
                                           const ActiveBackend &active);

  /* retrieval & grouping */
  /// Fill `budgetTokens` with the most relevant chunks, grouped by page.
  QList<PageContext> retrieveAndGroupPages(const QString &queryText,
                                           int budgetTokens) const;
  /// Token count with the embedding tokenizer (chars/3 if unavailable).
  int countTokens(const QString &text) const;

  /* prompt building */
  PromptPayload buildPrompt(const QList<PageContext> &pages,
                            const QString &queryText) const;
  QStringList buildCitationsFooter(const QList<PageContext> &pages,
                                   const QSet<int> &cited) const;

  /* result handling */
  std::expected<void, QString> saveQuery(const QString &queryText,
                                         const QString &response,
                                         const QString &modelName);

  static const QString &systemPrompt();

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
