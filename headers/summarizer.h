#pragma once

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <vector>

#include "headers/helpers.h"
#include "headers/localbackend.h"

class ConfigManager;
class DatabaseManager;
class RemoteBackend;

/// Orchestrator for paper summarization.
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
  /* backend resolution */
  struct BackendInfo {
    bool isLocal;
    QString modelName;
    QString modelPath;
    QVariantMap config;
  };

  BackendInfo resolveBackend(ConfigManager *config);

  /* GPU lifecycle */
  bool prepareGpu(bool isLocal);
  void cleanupGpu(bool isLocal, bool wasEmbeddingReady);

  /* inference */
  QString summarizeLocal(const PromptPayload &payload,
                         const BackendInfo &backend, ConfigManager *config);
  QString summarizeRemote(const PromptPayload &payload,
                          const BackendInfo &backend, ConfigManager *config);

  /* prompt building */
  PromptPayload buildPrompt(const QString &paperHash) const;
  QString retrieveContext(const QString &paperHash) const;
  std::vector<PageImage> retrieveVisualContext(const QString &paperHash) const;

  /* result handling */
  void saveSummary(const QString &hash, const PaperSummary &summary,
                   const QString &rawJson, const QString &modelName);

  static const QString &systemPrompt();
  static const QString &userPrompt();

  ConfigManager *m_config = nullptr;
  DatabaseManager *m_database = nullptr;
  LocalBackend *m_chatBackend = nullptr;
  RemoteBackend *m_remoteBackend = nullptr;
};
