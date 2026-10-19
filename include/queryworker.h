#pragma once

#include <QObject>
#include <QVariantMap>
#include <vector>

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

public slots:
  /// Execute a RAG query.
  /// \param queryText    the user's natural-language question
  /// \param maxPages     hard cap on the number of unique pages to consider
  void executeQuery(const QString &queryText, int maxPages);
  void stop();

signals:
  void queryFinished(const QString &response, bool success,
                     const QString &error);
  void started();
  void stopped();

private:
  /* backend resolution */
  struct BackendInfo {
    bool isLocal;
    QString modelName;
    QString modelPath;
    QVariantMap config;
  };

  BackendInfo resolveBackend();

  /* GPU lifecycle */
  bool prepareGpu(bool isLocal);
  void cleanupGpu(bool isLocal, bool wasEmbeddingReady);

  /* inference */
  QString queryLocal(const PromptPayload &payload, const BackendInfo &backend);
  QString queryRemote(const PromptPayload &payload, const BackendInfo &backend);

  /* retrieval & grouping */
  QList<PageContext> retrieveAndGroupPages(const QString &queryText,
                                           int maxPages) const;

  /* prompt building */
  PromptPayload buildPrompt(const QList<PageContext> &pages,
                            const QString &queryText) const;
  QStringList buildCitationsFooter(const QList<PageContext> &pages) const;

  /* result handling */
  void saveQuery(const QString &queryText, const QString &response,
                 const QString &modelName);

  static const QString &systemPrompt();

  bool isStopping() const { return m_stopping; }

  // Called at the start of each executeQuery so a cancellation from a
  // previous, abandoned run cannot bleed into the next one.
  void resetCancellation() { m_cancelled = false; }

  bool m_stopping = false;
  // Tokens reserved for the generated response when sizing the local model's
  // KV cache (prompt_tokens + m_headroom). RAG answers synthesize across
  // multiple papers with citations, so this is larger than the summary
  // headroom.
  uint32_t m_headroom = 4096;
  bool m_cancelled = false;
  ConfigManager *m_config = nullptr;
  DatabaseManager *m_database = nullptr;
  LocalBackend *m_chatBackend = nullptr;
  RemoteBackend *m_remoteBackend = nullptr;
};
