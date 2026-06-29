#pragma once

#include <QObject>
#include <QStringList>
#include <vector>

#include "headers/llama_chat_backend.h"

class ConfigManager;
class DatabaseManager;
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
class Summarizer : public QObject {
  Q_OBJECT

public:
  explicit Summarizer(ConfigManager *config, DatabaseManager *database,
                      QObject *parent = nullptr);
  ~Summarizer() override;

public slots:
  void summarizePapers(const QStringList &hashes, ConfigManager *config);
  void stop();

signals:
  void paperFinished(const QString &hash, bool success, const QString &message);
  void allFinished(int successCount, int total);
  void started();

private:
  QString retrieveContext(const QString &paperHash) const;
  std::vector<PageImage> retrieveVisualContext(const QString &paperHash) const;
  PromptPayload buildPrompt(const QString &paperHash) const;
  /// Get the system prompt template sent to the LLM.
  static const QString &systemPrompt();
  /// Get the user prompt prefix (e.g. "Summarize this paper:").
  static const QString &userPrompt();

  ConfigManager *m_config = nullptr;
  DatabaseManager *m_database = nullptr;
  LlamaChatBackend *m_chatBackend = nullptr;
  RemoteBackend *m_remoteBackend = nullptr;
};
