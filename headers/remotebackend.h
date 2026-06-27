#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

/// Remote backend using QNetworkAccessManager.
///
/// Sends chat completion requests to a distant server
/// (OpenAI, Ollama, LM Studio, etc.) via HTTP POST to /v1/chat/completions.
/// Parses the JSON response and returns the assistant message content.
class RemoteBackend : public QObject {
  Q_OBJECT

  Q_PROPERTY(QString apiUrl READ apiUrl WRITE setApiUrl)
  Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey)
  Q_PROPERTY(QString modelName READ modelName WRITE setModelName)
  Q_PROPERTY(float temperature READ temperature WRITE setTemperature)
  Q_PROPERTY(int maxTokens READ maxTokens WRITE setMaxTokens)

public:
  explicit RemoteBackend(QObject *parent = nullptr);
  ~RemoteBackend() override;

  // Configuration
  QString apiUrl() const { return m_apiUrl; }
  QString apiKey() const { return m_apiKey; }
  QString modelName() const { return m_modelName; }
  float temperature() const { return m_temperature; }
  int maxTokens() const { return m_maxTokens; }

  void setApiUrl(const QString &url);
  void setApiKey(const QString &key);
  void setModelName(const QString &name);
  void setTemperature(float temp);
  void setMaxTokens(int tokens);

  /// Apply settings from ConfigManager (convenience wrapper).
  void applyConfig();

  /// Whether the backend has a valid configuration.
  bool isReady() const;

  /// Generate a completion from system + user prompts.
  /// Asynchronous; result delivered via the finished() signal.
  void generate(const QString &systemPrompt, const QString &userPrompt);

  /// Cancel an in-flight request.
  void cancel();

  /// Fetch the list of available models from the remote API.
  /// Sends GET /v1/models using the given URL and key (does not modify
  /// the persistent backend configuration). Emits modelsReady() with the
  /// model IDs.
  void fetchModels(const QString &apiUrl, const QString &apiKey);

signals:
  /// Emitted when generation completes (success or failure).
  /// @param text  Assistant message content (empty on error).
  /// @param error Error description (empty on success).
  void finished(const QString &text, const QString &error);

  /// Emitted after fetchModels() completes.
  /// @param models List of model IDs (empty on error).
  /// @param error  Error description (empty on success).
  void modelsReady(const QStringList &models, const QString &error);

  void statusUpdated(const QString &msg);

private:
  void handleReply(QNetworkReply *reply);
  void handleModelsReply(QNetworkReply *reply);

  QNetworkAccessManager *m_manager = nullptr;
  QNetworkReply *m_currentReply = nullptr;

  QString m_apiUrl; // e.g. "http://localhost:11434/v1"
  QString m_apiKey;
  QString m_modelName;
  float m_temperature = 0.1f;
  int m_maxTokens = 4096;
};
