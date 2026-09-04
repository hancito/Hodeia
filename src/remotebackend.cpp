#include "remotebackend.h"

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

RemoteBackend::RemoteBackend(QObject *parent) : QObject(parent) {
  m_manager = new QNetworkAccessManager(this);
  connect(m_manager, &QNetworkAccessManager::sslErrors, this,
          [this](QNetworkReply *, const QList<QSslError> &errors) {
            QString details;
            for (const auto &e : errors)
              details += e.errorString() + " ";
            qDebug() << "[RemoteBackend] SSL error(s):" << details;
            emit statusUpdated(tr("SSL error: %1").arg(details.trimmed()));
          });
}

RemoteBackend::~RemoteBackend() = default;

/* ── Detection helpers ──────────────────────────────────────────── */

bool RemoteBackend::isOllamaLocal() const {
  return m_apiUrl.contains("localhost:11434");
}

QJsonObject RemoteBackend::ollamaOptions() const {
  QJsonObject options;
  options["num_ctx"] = 256000;
  options["num_predict"] = m_maxTokens;
  QJsonObject thinking;
  thinking["enabled"] = false;
  options["thinking"] = thinking;
  return options;
}

/* ── URL / request builders ─────────────────────────────────────── */

QUrl RemoteBackend::buildCompletionUrl() const {
  QUrl url(m_apiUrl);
  if (url.path().endsWith("/v1"))
    url.setPath(url.path().chopped(3));
  url.setPath(url.path() + "/v1/chat/completions");
  return url;
}

QNetworkRequest RemoteBackend::buildRequest(const QUrl &url) const {
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  if (!m_apiKey.isEmpty())
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
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

/* ── Message builders ───────────────────────────────────────────── */

QJsonObject RemoteBackend::systemMessage(const QString &prompt) const {
  QJsonObject msg;
  msg["role"] = "system";
  msg["content"] = prompt;
  return msg;
}

QJsonObject RemoteBackend::textUserMessage(const QString &prompt) const {
  QJsonObject msg;
  msg["role"] = "user";
  msg["content"] = prompt;
  return msg;
}

QJsonObject
RemoteBackend::visionUserMessage(const QString &prompt,
                                 const std::vector<PageImage> &images) const {
  QJsonArray contentParts;

  QJsonObject textPart;
  textPart["type"] = "text";
  textPart["text"] = prompt;
  contentParts.append(textPart);

  for (const auto &img : images)
    contentParts.append(imageToPart(img));

  QJsonObject msg;
  msg["role"] = "user";
  msg["content"] = contentParts;
  return msg;
}

QJsonObject RemoteBackend::imageToPart(const PageImage &img) const {
  QImage image(img.data.data(), img.width, img.height, QImage::Format_RGB888);
  image = image.copy();

  QByteArray pngData;
  QBuffer buffer(&pngData);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  buffer.close();

  QJsonObject part;
  part["type"] = "image_url";
  QJsonObject imgUrl;
  imgUrl["url"] = QString("data:image/png;base64,") +
                  QString::fromLatin1(pngData.toBase64());
  part["image_url"] = imgUrl;

  return part;
}

/* ── Request body ───────────────────────────────────────────────── */

QJsonObject RemoteBackend::buildBody(const QJsonArray &messages) const {
  QJsonObject body;
  body["model"] = m_modelName;
  body["messages"] = messages;
  body["temperature"] = m_temperature;
  body["max_tokens"] = m_maxTokens;
  body["stream"] = false;

  if (m_forceJson) {
    QJsonObject responseFormat;
    responseFormat["type"] = "json_object";
    body["response_format"] = responseFormat;
  }

  if (isOllamaLocal()) {
    body["options"] = ollamaOptions();
  }

  return body;
}

/* ── Public generate overloads ──────────────────────────────────── */

void RemoteBackend::generate(const QString &systemPrompt,
                             const QString &userPrompt) {
  if (!isReady()) {
    emit finished(QString(), "Backend not configured (URL or model missing)");
    return;
  }
  cancel();

  QJsonArray messages;
  if (!systemPrompt.isEmpty())
    messages.append(systemMessage(systemPrompt));
  messages.append(textUserMessage(userPrompt));

  sendRequest(
      buildRequest(buildCompletionUrl()),
      QJsonDocument(buildBody(messages)).toJson(QJsonDocument::Compact));
}

void RemoteBackend::generate(const QString &systemPrompt,
                             const QString &userPrompt,
                             const std::vector<PageImage> &images) {
  if (!isReady()) {
    emit finished(QString(), "Backend not configured (URL or model missing)");
    return;
  }
  cancel();

  qDebug() << "[RemoteBackend] Sending request with" << images.size()
           << "image(s)";

  QJsonArray messages;
  if (!systemPrompt.isEmpty())
    messages.append(systemMessage(systemPrompt));
  messages.append(visionUserMessage(userPrompt, images));

  sendRequest(
      buildRequest(buildCompletionUrl()),
      QJsonDocument(buildBody(messages)).toJson(QJsonDocument::Compact));
}

void RemoteBackend::generateInterleaved(const QString &systemPrompt,
                                        const PromptPayload &payload,
                                        bool hasImages) {
  if (!isReady()) {
    emit finished(QString(), "Backend not configured (URL or model missing)");
    return;
  }
  cancel();

  if (!hasImages) {
    // Text-only: concatenate all page text
    QString fullText = payload.userMessage + "\n\n## DOCUMENT\n\n";
    for (const auto &pb : payload.pageBlocks)
      fullText += QString("## Page %1\n%2\n\n").arg(pb.pageNumber).arg(pb.text);

    QJsonArray messages;
    if (!systemPrompt.isEmpty())
      messages.append(systemMessage(systemPrompt));
    messages.append(textUserMessage(fullText));

    // ── Debug ──
    qDebug() << "========== PROMPT DEBUG (remote/text-only) ==========";
    qDebug() << "SYSTEM:" << systemPrompt.left(200) << "...";
    QString dbg = fullText;
    if (dbg.length() > 4000)
      dbg =
          dbg.left(4000) +
          QString("\n... [truncated, total: %1 chars]").arg(fullText.length());
    qDebug().noquote() << "USER:" << dbg;
    qDebug() << "====================================================";

    sendRequest(
        buildRequest(buildCompletionUrl()),
        QJsonDocument(buildBody(messages)).toJson(QJsonDocument::Compact));
    return;
  }

  // Multimodal: interleaved multi-part content
  // Format: text → image → text → image … per page
  QJsonArray contentParts;
  {
    QJsonObject part;
    part["type"] = "text";
    part["text"] = payload.userMessage + "\n\n## DOCUMENT\n\n";
    contentParts.append(part);
  }

  for (const auto &pb : payload.pageBlocks) {
    // Image FIRST, then text
    if (!pb.visualContext.data.empty())
      contentParts.append(imageToPart(pb.visualContext));

    {
      QJsonObject part;
      part["type"] = "text";
      part["text"] =
          QString("## Page %1\n%2\n\n").arg(pb.pageNumber).arg(pb.text);
      contentParts.append(part);
    }
  }

  QJsonObject msg;
  msg["role"] = "user";
  msg["content"] = contentParts;

  QJsonArray messages;
  if (!systemPrompt.isEmpty())
    messages.append(systemMessage(systemPrompt));
  messages.append(msg);

  // ── Debug ──
  qDebug() << "========== PROMPT DEBUG (remote/multimodal) ==========";
  qDebug() << "SYSTEM:" << systemPrompt.left(200) << "...";
  qDebug() << "Content parts:" << contentParts.size() << "(for"
           << payload.pageBlocks.size() << "pages)";
  for (int i = 0; i < contentParts.size(); ++i) {
    QJsonObject part = contentParts[i].toObject();
    if (part.value("type") == "text") {
      QString t = part.value("text").toString();
      if (t.length() > 800)
        t = t.left(800) + "...";
      qDebug().noquote() << "  part[" << i << "] text:" << t;
    } else {
      qDebug()
          << "  part[" << i << "] image ("
          << part.value("image_url").toObject().value("url").toString().left(60)
          << "...)";
    }
  }
  qDebug() << "======================================================";

  sendRequest(
      buildRequest(buildCompletionUrl()),
      QJsonDocument(buildBody(messages)).toJson(QJsonDocument::Compact));
}

void RemoteBackend::cancel() {
  if (m_currentReply) {
    m_currentReply->abort();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;
  }
}

/* ── Response handling ──────────────────────────────────────────── */

void RemoteBackend::handleReply(QNetworkReply *reply) {
  if (!reply)
    return;

  // Network-level errors
  if (reply->error() != QNetworkReply::NoError) {
    QString err = reply->errorString();
    qDebug() << "[RemoteBackend] Network error:" << err;
    emit finished(QString(), err);
    reply->deleteLater();
    return;
  }

  QByteArray data = reply->readAll();
  reply->deleteLater();

  // JSON parse
  QJsonParseError parseErr;
  QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
  if (parseErr.error != QJsonParseError::NoError) {
    QString err = tr("JSON parse error: %1").arg(parseErr.errorString());
    qDebug() << "[RemoteBackend]" << err;
    emit finished(QString(), err);
    return;
  }

  // Extract assistant content
  QJsonArray choices = doc.object().value("choices").toArray();
  if (choices.isEmpty()) {
    qDebug() << "[RemoteBackend] No choices in response";
    emit finished(QString(), "Empty response from server");
    return;
  }

  QJsonObject message = choices.first().toObject().value("message").toObject();
  QString content = message.value("content").toString();

  // Fallback: reasoning models put output in "reasoning"
  if (content.isEmpty()) {
    content = message.value("reasoning").toString();
    if (!content.isEmpty())
      qDebug() << "[RemoteBackend] Used reasoning field as fallback"
               << content.size() << "chars";
  }

  if (content.isEmpty()) {
    auto firstChoice = choices.first().toObject();
    qDebug() << "[RemoteBackend] Empty content — finish_reason:"
             << firstChoice.value("finish_reason");
    emit finished(QString(), "Empty content in response");
    return;
  }

  emit statusUpdated(tr("Response received (%1 chars)").arg(content.length()));
  emit finished(content, QString());
}
