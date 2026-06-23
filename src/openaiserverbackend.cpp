#include "headers/openaiserverbackend.h"
#include "headers/configmanager.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

OpenAiServerBackend::OpenAiServerBackend(QObject *parent) : QObject(parent) {
  m_manager = new QNetworkAccessManager(this);
}

OpenAiServerBackend::~OpenAiServerBackend() = default;

void OpenAiServerBackend::setApiUrl(const QString &url) { m_apiUrl = url; }

void OpenAiServerBackend::setApiKey(const QString &key) { m_apiKey = key; }

void OpenAiServerBackend::setModelName(const QString &name) {
  m_modelName = name;
}

void OpenAiServerBackend::setTemperature(float temp) { m_temperature = temp; }

void OpenAiServerBackend::setMaxTokens(int tokens) { m_maxTokens = tokens; }

void OpenAiServerBackend::applyConfig() {
  // Placeholder — the Summarizer passes config values directly.
}

bool OpenAiServerBackend::isReady() const {
  return !m_apiUrl.isEmpty() && !m_modelName.isEmpty();
}

void OpenAiServerBackend::generate(const QString &systemPrompt,
                                   const QString &userPrompt) {
  if (!isReady()) {
    emit finished(QString(), "Backend not configured (URL or model missing)");
    return;
  }

  cancel();

  QUrl baseUrl(m_apiUrl);
  // Strip trailing /v1 so we can append cleanly
  if (baseUrl.path().endsWith("/v1")) {
    baseUrl.setPath(baseUrl.path().chopped(3));
  }
  baseUrl.setPath(baseUrl.path() + "/v1/chat/completions");

  // Build messages array
  QJsonArray messages;
  if (!systemPrompt.isEmpty()) {
    QJsonObject sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = systemPrompt;
    messages.append(sysMsg);
  }
  QJsonObject userMsg;
  userMsg["role"] = "user";
  userMsg["content"] = userPrompt;
  messages.append(userMsg);

  // Build request body
  QJsonObject body;
  body["model"] = m_modelName;
  body["messages"] = messages;
  body["temperature"] = m_temperature;
  body["max_tokens"] = m_maxTokens;

  QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

  // Build HTTP request
  QNetworkRequest request(baseUrl);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  if (!m_apiKey.isEmpty()) {
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
  }

  emit statusUpdated(tr("Sending request to %1").arg(baseUrl.toString()));

  m_currentReply = m_manager->post(request, payload);
  QObject::connect(m_currentReply, &QNetworkReply::finished, this,
                   [this, reply = m_currentReply]() {
                     m_currentReply = nullptr;
                     handleReply(reply);
                   });
}

void OpenAiServerBackend::cancel() {
  if (m_currentReply) {
    m_currentReply->abort();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;
  }
}

void OpenAiServerBackend::handleReply(QNetworkReply *reply) {
  if (!reply)
    return;

  if (reply->error() != QNetworkReply::NoError) {
    QString err = reply->errorString();
    qDebug() << "[OpenAiServerBackend] Network error:" << err;
    emit finished(QString(), err);
    reply->deleteLater();
    return;
  }

  QByteArray data = reply->readAll();
  reply->deleteLater();

  QJsonParseError parseErr;
  QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
  if (parseErr.error != QJsonParseError::NoError) {
    QString err = tr("JSON parse error: %1").arg(parseErr.errorString());
    qDebug() << "[OpenAiServerBackend]" << err;
    emit finished(QString(), err);
    return;
  }

  // Extract choices[0].message.content
  QJsonObject root = doc.object();
  QJsonArray choices = root.value("choices").toArray();
  if (choices.isEmpty()) {
    emit finished(QString(), "Empty response from server");
    return;
  }

  QJsonObject firstChoice = choices.at(0).toObject();
  QJsonObject message = firstChoice.value("message").toObject();
  QString content = message.value("content").toString();

  if (content.isEmpty()) {
    emit finished(QString(), "Empty content in response");
    return;
  }

  emit statusUpdated(tr("Response received (%1 chars)").arg(content.length()));
  emit finished(content, QString());
}
