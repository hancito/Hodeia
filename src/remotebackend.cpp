#include "headers/remotebackend.h"
#include "headers/configmanager.h"

#include <QBuffer>
#include <QDebug>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslError>
#include <QUrl>
#include <QUrlQuery>

RemoteBackend::RemoteBackend(QObject *parent) : QObject(parent) {
  m_manager = new QNetworkAccessManager(this);
  connect(m_manager, &QNetworkAccessManager::sslErrors, this,
          [this](QNetworkReply *rep, const QList<QSslError> &errors) {
            QString details;
            for (const auto &e : errors)
              details += e.errorString() + " ";
            qDebug() << "[RemoteBackend] SSL error(s):" << details;
            emit statusUpdated(tr("SSL error: %1").arg(details.trimmed()));
          });
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

// ── Private helpers ────────────────────────────────────────────────

QUrl RemoteBackend::buildCompletionUrl() const {
  QUrl url(m_apiUrl);
  if (url.path().endsWith("/v1")) {
    url.setPath(url.path().chopped(3));
  }
  url.setPath(url.path() + "/v1/chat/completions");
  return url;
}

QNetworkRequest RemoteBackend::buildRequest(const QUrl &url) const {
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  if (!m_apiKey.isEmpty()) {
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
  }
  return request;
}

void RemoteBackend::sendRequest(QNetworkRequest request,
                                const QByteArray &payload) {
  emit statusUpdated(tr("Sending request to %1").arg(request.url().toString()));

  m_currentReply = m_manager->post(request, payload);
  QObject::connect(m_currentReply, &QNetworkReply::finished, this,
                   [this, reply = m_currentReply]() {
                     m_currentReply = nullptr;
                     handleReply(reply);
                   });
}

QJsonObject RemoteBackend::buildBody(const QJsonArray &messages) const {
  QJsonObject body;
  body["model"] = m_modelName;
  body["messages"] = messages;
  body["temperature"] = m_temperature;
  body["max_tokens"] = m_maxTokens;
  return body;
}

// ── Public API ─────────────────────────────────────────────────────

void RemoteBackend::generate(const QString &systemPrompt,
                             const QString &userPrompt) {
  if (!isReady()) {
    emit finished(QString(), "Backend not configured (URL or model missing)");
    return;
  }
  cancel();

  // Build messages array (text-only)
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

  QUrl url = buildCompletionUrl();
  QNetworkRequest request = buildRequest(url);
  QByteArray payload =
      QJsonDocument(buildBody(messages)).toJson(QJsonDocument::Compact);
  sendRequest(request, payload);
}

void RemoteBackend::generate(const QString &systemPrompt,
                             const QString &userPrompt,
                             const std::vector<PageImage> &images) {
  if (!isReady()) {
    emit finished(QString(), "Backend not configured (URL or model missing)");
    return;
  }
  cancel();

  // Build messages array with vision format
  QJsonArray messages;
  if (!systemPrompt.isEmpty()) {
    QJsonObject sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = systemPrompt;
    messages.append(sysMsg);
  }

  // User message: content as array [text, image_url, image_url, ...]
  QJsonArray contentParts;
  QJsonObject textPart;
  textPart["type"] = "text";
  textPart["text"] = userPrompt;
  contentParts.append(textPart);

  for (size_t i = 0; i < images.size(); ++i) {
    QImage img(images[i].data.data(), images[i].width, images[i].height,
               QImage::Format_RGB888);
    img = img.copy(); // QImage does not copy on construction from external ptr

    QByteArray pngData;
    QBuffer buffer(&pngData);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");
    buffer.close();

    QJsonObject imgUrlPart;
    imgUrlPart["type"] = "image_url";
    QJsonObject imgUrl;
    imgUrl["url"] = QString("data:image/png;base64,") +
                    QString::fromLatin1(pngData.toBase64());
    imgUrlPart["image_url"] = imgUrl;
    contentParts.append(imgUrlPart);
  }

  QJsonObject userMsg;
  userMsg["role"] = "user";
  userMsg["content"] = contentParts;
  messages.append(userMsg);

  qDebug() << "[RemoteBackend] Sending request with" << images.size()
           << "image(s)";

  QUrl url = buildCompletionUrl();
  QNetworkRequest request = buildRequest(url);
  QByteArray payload =
      QJsonDocument(buildBody(messages)).toJson(QJsonDocument::Compact);
  sendRequest(request, payload);
}

void RemoteBackend::cancel() {
  if (m_currentReply) {
    m_currentReply->abort();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;
  }
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
