#include "headers/remotebackend.h"
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

RemoteBackend::RemoteBackend(QObject *parent) : QObject(parent) {
  m_manager = new QNetworkAccessManager(this);
}

RemoteBackend::~RemoteBackend() = default;

void RemoteBackend::setApiUrl(const QString &url) { m_apiUrl = url; }

void RemoteBackend::setApiKey(const QString &key) { m_apiKey = key; }

void RemoteBackend::setModelName(const QString &name) { m_modelName = name; }

void RemoteBackend::setTemperature(float temp) { m_temperature = temp; }

void RemoteBackend::setMaxTokens(int tokens) { m_maxTokens = tokens; }

void RemoteBackend::applyConfig() {
  // Placeholder — the Summarizer passes config values directly.
}

bool RemoteBackend::isReady() const {
  return !m_apiUrl.isEmpty() && !m_modelName.isEmpty();
}

void RemoteBackend::generate(const QString &systemPrompt,
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

void RemoteBackend::cancel() {
  if (m_currentReply) {
    m_currentReply->abort();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;
  }
}

void RemoteBackend::fetchModels(const QString &apiUrl, const QString &apiKey) {
  if (apiUrl.isEmpty()) {
    emit modelsReady(QStringList(), "URL is empty");
    return;
  }

  cancel();

  QUrl baseUrl(apiUrl);
  baseUrl.setPath(baseUrl.path() + "/models");

  QNetworkRequest request(baseUrl);
  if (!apiKey.isEmpty()) {
    request.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
  }

  emit statusUpdated(tr("Fetching model list from %1").arg(baseUrl.toString()));

  m_currentReply = m_manager->get(request);
  QObject::connect(m_currentReply, &QNetworkReply::finished, this,
                   [this, reply = m_currentReply]() {
                     m_currentReply = nullptr;
                     handleModelsReply(reply);
                   });
}

void RemoteBackend::handleReply(QNetworkReply *reply) {
  if (!reply)
    return;

  if (reply->error() != QNetworkReply::NoError) {
    QString err = reply->errorString();
    qDebug() << "[RemoteBackend] Network error:" << err;
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
    qDebug() << "[RemoteBackend]" << err;
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

void RemoteBackend::handleModelsReply(QNetworkReply *reply) {
  if (!reply)
    return;

  if (reply->error() != QNetworkReply::NoError) {
    QString err = reply->errorString();
    qDebug() << "[RemoteBackend] Network error during models fetch:" << err;
    emit modelsReady(QStringList(), err);
    reply->deleteLater();
    return;
  }

  QByteArray data = reply->readAll();
  reply->deleteLater();

  QJsonParseError parseErr;
  QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
  if (parseErr.error != QJsonParseError::NoError) {
    QString err = tr("JSON parse error: %1").arg(parseErr.errorString());
    qDebug() << "[RemoteBackend]" << err;
    emit modelsReady(QStringList(), err);
    return;
  }

  QJsonObject root = doc.object();
  QJsonArray dataArray = root.value("data").toArray();

  QStringList models;
  for (const auto &elem : dataArray) {
    QJsonObject m = elem.toObject();
    QString id = m.value("id").toString();
    if (!id.isEmpty()) {
      models.append(id);
    }
  }

  emit statusUpdated(tr("Received %1 model(s) from server").arg(models.size()));
  emit modelsReady(models, QString());
}
