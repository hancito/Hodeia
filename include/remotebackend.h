#pragma once

#include <QObject>
#include <QString>
#include <vector>

#include "localbackend.h"

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QUrl;

/// Sends chat-completion requests to an OpenAI-compatible endpoint
/// (Ollama, LM Studio, etc.) via HTTP POST to /v1/chat/completions.
class RemoteBackend : public QObject {
  Q_OBJECT

  Q_PROPERTY(QString apiUrl READ apiUrl WRITE setApiUrl)
  Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey)
  Q_PROPERTY(QString modelName READ modelName WRITE setModelName)

public:
  explicit RemoteBackend(QObject *parent = nullptr);
  ~RemoteBackend() override;

  // Configuration
  QString apiUrl() const { return m_apiUrl; }
  QString apiKey() const { return m_apiKey; }
  QString modelName() const { return m_modelName; }

  void setApiUrl(const QString &url) { m_apiUrl = url; }
  void setApiKey(const QString &key) { m_apiKey = key; }
  void setModelName(const QString &name) { m_modelName = name; }

  /// Whether the backend has a valid URL and model name.
  bool isReady() const { return !m_apiUrl.isEmpty() && !m_modelName.isEmpty(); }

  /// Force JSON response format (for structured outputs like summaries).
  void setForceJson(bool on) { m_forceJson = on; }

  /// Generate with interleaved page-by-page content (image before text per
  /// page).
  void generateInterleaved(const QString &systemPrompt,
                           const PromptPayload &payload, bool hasImages);

  /// Cancel an in-flight request.
  void cancel();

  /// A page image as an OpenAI "image_url" content part (PNG data URL).
  static QJsonObject imageToPart(const PageImage &img);

signals:
  void finished(const QString &text, const QString &error);
  void statusUpdated(const QString &msg);

private:
  QUrl buildCompletionUrl() const;
  QNetworkRequest buildRequest(const QUrl &url) const;
  void sendRequest(QNetworkRequest request, const QByteArray &payload);

  QJsonObject buildBody(const QJsonArray &messages) const;
  QJsonObject ollamaOptions() const;
  bool isOllamaLocal() const;

  QJsonObject systemMessage(const QString &prompt) const;
  QJsonObject textUserMessage(const QString &prompt) const;

  void handleReply(QNetworkReply *reply);

  QNetworkAccessManager *m_manager = nullptr;
  QNetworkReply *m_currentReply = nullptr;

  QString m_apiUrl;
  QString m_apiKey;
  QString m_modelName;
  static constexpr float kTemperature = 0.1f;
  static constexpr int kMaxTokens = 8192;
  bool m_forceJson = false;
};
