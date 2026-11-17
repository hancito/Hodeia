#include "anthropicapi.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDebug>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>

namespace AnthropicApi {

QUrl endpointUrl(const QString &base, const QString &endpoint) {
  QUrl url(base.trimmed());
  QString path = url.path();
  while (path.endsWith('/'))
    path.chop(1);
  if (path.endsWith("/v1"))
    path.chop(3);
  url.setPath(path + "/v1/" + endpoint);
  return url;
}

bool withFallbacks(const QString &model) {
  static const QStringList kModels = {
      "claude-fable-5-2", "claude-fable-5-1", "claude-opus-5-5",
      "claude-opus-5", "claude-sonnet-5-5"};
  return kModels.contains(model);
}

void applyHeaders(QNetworkRequest &request, const QString &apiKey,
                  const QString &model) {
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  request.setRawHeader("x-api-key", apiKey.toUtf8());
  request.setRawHeader("anthropic-version", kApiVersion);
  if (withFallbacks(model))
    request.setRawHeader("anthropic-beta", "server-side-fallback-2026-07-01");
}

static QJsonObject textBlock(const QString &text) {
  return QJsonObject{{"type", "text"}, {"text", text}};
}

static QJsonObject imageBlock(const PageImage &img) {
  QImage image(img.data.data(), img.width, img.height, img.width * 3,
               QImage::Format_RGB888);
  QByteArray jpeg;
  QBuffer buffer(&jpeg);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "JPEG", 85);
  return QJsonObject{
      {"type", "image"},
      {"source", QJsonObject{{"type", "base64"},
                             {"media_type", "image/jpeg"},
                             {"data", QString::fromLatin1(jpeg.toBase64())}}}};
}

QJsonObject buildBody(const PromptPayload &payload, const QString &model) {
  QJsonArray content;
  if (!payload.pageBlocks.empty()) {
    // Same layout as RemoteBackend::generateInterleaved: instructions, then
    // per page its image (when any) before its text.
    content.append(textBlock(payload.userMessage + "\n\n## DOCUMENT\n\n"));
    for (const auto &pb : payload.pageBlocks) {
      if (!pb.visualContext.data.empty())
        content.append(imageBlock(pb.visualContext));
      content.append(textBlock(
          QString("## Page %1\n%2\n\n").arg(pb.pageNumber).arg(pb.text)));
    }
  } else {
    content.append(
        textBlock(payload.userMessage + "\n\n" + payload.textContext));
    for (const auto &img : payload.visualContext)
      content.append(imageBlock(img));
  }

  QJsonObject body{
      {"model", model},
      {"max_tokens", kMaxTokens},
      {"messages",
       QJsonArray{QJsonObject{{"role", "user"}, {"content", content}}}}};
  if (!payload.systemPrompt.isEmpty())
    body["system"] = payload.systemPrompt;
  if (withFallbacks(model))
    body["fallbacks"] = "default";
  return body;
}

std::expected<QString, QString> parseResponse(const QByteArray &body,
                                              int httpStatus) {
  QJsonParseError parseErr;
  const QJsonObject obj = QJsonDocument::fromJson(body, &parseErr).object();
  if (parseErr.error != QJsonParseError::NoError) {
    return std::unexpected(
        httpStatus >= 400
            ? QCoreApplication::translate("AnthropicApi", "HTTP error %1")
                  .arg(httpStatus)
            : QCoreApplication::translate("AnthropicApi",
                                          "JSON parse error: %1")
                  .arg(parseErr.errorString()));
  }

  // {"type":"error","error":{"type":"...","message":"..."}}
  if (obj.value("type").toString() == "error") {
    const QJsonObject err = obj.value("error").toObject();
    return std::unexpected(QString("%1 (%2)").arg(
        err.value("message").toString(), err.value("type").toString()));
  }

  const QString stopReason = obj.value("stop_reason").toString();
  if (stopReason == "refusal") {
    const QString category = obj.value("stop_details")
                                 .toObject()
                                 .value("category")
                                 .toString();
    return std::unexpected(
        QCoreApplication::translate("AnthropicApi",
                                    "The model declined the request (%1)")
            .arg(category.isEmpty() ? QStringLiteral("refusal") : category));
  }

  QString text;
  for (const auto &block : obj.value("content").toArray()) {
    const QJsonObject b = block.toObject();
    if (b.value("type").toString() == "text")
      text += b.value("text").toString();
  }
  if (stopReason == "max_tokens")
    qWarning() << "[AnthropicApi] Response cut at max_tokens (" << kMaxTokens
               << ")";
  if (text.isEmpty())
    return std::unexpected(QCoreApplication::translate(
        "AnthropicApi", "Empty response from server"));
  return text;
}

} // namespace AnthropicApi
