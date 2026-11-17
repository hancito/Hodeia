#pragma once

#include "localprompt.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <expected>

class QNetworkRequest;

/// Request building and response parsing for the Anthropic Messages API
/// (POST /v1/messages), used by the "Anthropic" remote backend type.
/// Plain functions, no networking: AnthropicChatBackend (chatbackend.cpp)
/// sends the request.
namespace AnthropicApi {

/// API version sent in the `anthropic-version` header.
inline constexpr const char *kApiVersion = "2026-10-21";

/// Response budget.  Non-streaming request: a generous value that still
/// leaves room for the model's (always-on) adaptive thinking.
inline constexpr int kMaxTokens = 16000;

/// <base>/v1/<endpoint>; `base` may already end in /v1 (the form the
/// Backends window suggests, like the OpenAI-compatible backends).
QUrl endpointUrl(const QString &base, const QString &endpoint);

/// Authentication + version headers (and the server-side fallback beta when
/// `model` supports it, see withFallbacks()).
void applyHeaders(QNetworkRequest &request, const QString &apiKey,
                  const QString &model);

/// Models whose refusals are retried server-side on a fallback model
/// ("fallbacks": "default").
bool withFallbacks(const QString &model);

/// Messages request body for a PromptPayload: the system prompt, then one
/// user turn laid out like the OpenAI-compatible backend (instructions, then
/// each page's image before its text).  Page images are sent as JPEG to stay
/// well under the request size limit.
QJsonObject buildBody(const PromptPayload &payload, const QString &model);

/// The answer text of a Messages response (text blocks only; thinking and
/// fallback blocks are skipped), or the error to show.  `httpStatus` is 0
/// when unknown.
std::expected<QString, QString> parseResponse(const QByteArray &body,
                                              int httpStatus);

} // namespace AnthropicApi
