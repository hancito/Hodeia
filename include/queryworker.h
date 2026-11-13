#pragma once

#include <QObject>
#include <QSet>
#include <QVariantMap>
#include <expected>

#include "localbackend.h"

class ConfigManager;
class DatabaseManager;
class RemoteBackend;

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

  struct BackendInfo {
    bool isLocal;
    QString modelName;
    QString modelPath;
    QVariantMap config;
  };
  /// The backend and model a query would use right now.
  static BackendInfo resolveBackend(ConfigManager *config);

public slots:
  /// Execute a RAG query.
  /// \param queryText      the user's natural-language question
  /// \param contextTokens  prompt budget in tokens; 0 = auto (largest that
  ///                       runs fully on the GPU for a local model)
  void executeQuery(const QString &queryText, int contextTokens);
  void stop();

signals:
  void queryFinished(const QString &response, bool success,
                     const QString &error);
  void started();
  void stopped();

private:
  /* GPU lifecycle */
  bool prepareGpu(bool isLocal);
  void cleanupGpu(bool isLocal, bool wasEmbeddingReady);

  /* inference — return the generated text, or a user-facing error message */
  std::expected<QString, QString> queryLocal(const PromptPayload &payload,
                                             const BackendInfo &backend);
  std::expected<QString, QString> queryRemote(const PromptPayload &payload,
                                              const BackendInfo &backend);

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
  void saveQuery(const QString &queryText, const QString &response,
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
  LocalBackend *m_chatBackend = nullptr;
  RemoteBackend *m_remoteBackend = nullptr;
};
