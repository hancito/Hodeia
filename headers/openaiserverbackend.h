#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

/// Remote OpenAI-compatible backend using QNetworkAccessManager.
///
/// Sends chat completion requests to a distant server
/// (OpenAI, Ollama, LM Studio, etc.) via HTTP POST to /v1/chat/completions.
/// Parses the JSON response and returns the assistant message content.
class OpenAiServerBackend : public QObject {
  Q_OBJECT

  Q_PROPERTY(QString apiUrl READ apiUrl WRITE setApiUrl)
  Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey)
  Q_PROPERTY(QString modelName READ modelName WRITE setModelName)
  Q_PROPERTY(float temperature READ temperature WRITE setTemperature)
  Q_PROPERTY(int maxTokens READ maxTokens WRITE setMaxTokens)

public:
  explicit OpenAiServerBackend(QObject *parent = nullptr);
  ~OpenAiServerBackend() override;

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
  Q_INVOKABLE void generate(const QString &systemPrompt,
                            const QString &userPrompt);

  /// Cancel an in-flight request.
  Q_INVOKABLE void cancel();

signals:
  /// Emitted when generation completes (success or failure).
  /// @param text  Assistant message content (empty on error).
  /// @param error Error description (empty on success).
  void finished(const QString &text, const QString &error);

  void statusUpdated(const QString &msg);

private:
  void handleReply(QNetworkReply *reply);

  QNetworkAccessManager *m_manager = nullptr;
  QNetworkReply *m_currentReply = nullptr;

  QString m_apiUrl; // e.g. "http://localhost:11434/v1"
  QString m_apiKey;
  QString m_modelName;
  float m_temperature = 0.1f;
  int m_maxTokens = 4096;
};
