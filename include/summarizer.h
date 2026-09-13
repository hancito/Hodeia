#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <vector>

#include "helpers.h"
#include "localbackend.h"

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
  void summarizePapers(const QStringList &hashes);
  void stop();

signals:
  void paperFinished(const QString &hash, bool success, const QString &message);
  void allFinished(int successCount, int total);
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
  QString summarizeLocal(const PromptPayload &payload,
                         const BackendInfo &backend);
  QString summarizeRemote(const PromptPayload &payload,
                          const BackendInfo &backend);

  /* prompt building */
  PromptPayload buildPrompt(const QString &paperHash) const;
  QString retrieveContext(const QString &paperHash) const;
  std::vector<PageImage> retrieveVisualContext(const QString &paperHash) const;
  std::vector<PageImage>
  retrieveReducedVisualContext(const QString &paperHash) const;

  /* result handling */
  void saveSummary(const QString &hash, const PaperSummary &summary,
                   const QString &rawJson, const QString &modelName);

  static const QString &systemPrompt();
  static const QString &userPrompt();

  bool isStopping() const { return m_stopping; }

  bool m_stopping = false;
  // Set by stop() to cooperatively cancel the in-flight local generation
  // (polled by InferenceEngine::generate()). A cancelled paper is not saved.
  bool m_cancelled = false;
  ConfigManager *m_config = nullptr;
  DatabaseManager *m_database = nullptr;
  LocalBackend *m_chatBackend = nullptr;
  RemoteBackend *m_remoteBackend = nullptr;
};
