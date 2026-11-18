#include "mcpserver.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <utility>

namespace {

// JSON-RPC 2.0 error codes, and the MCP-reserved ones (spec 2026-07-28,
// "error code allocation policy").
constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kUnsupportedProtocolVersion = -32022;

// _meta keys of the modern protocol.
const QString kMetaProtocolVersion =
    QStringLiteral("io.modelcontextprotocol/protocolVersion");
const QString kMetaServerInfo =
    QStringLiteral("io.modelcontextprotocol/serverInfo");

const QString kInstructions = QStringLiteral(
    "HodeIA serves the user's local library of research papers (PDFs, their "
    "text and summaries).");

QByteArray serialize(const QJsonObject &obj) {
  return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QByteArray resultResponse(const QJsonValue &id, const QJsonObject &result) {
  return serialize(
      QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
}

QJsonObject errorObject(int code, const QString &message,
                        const QJsonValue &data = QJsonValue::Undefined) {
  QJsonObject error{{"code", code}, {"message", message}};
  if (!data.isUndefined())
    error["data"] = data;
  return error;
}

QByteArray errorResponse(const QJsonValue &id, const QJsonObject &error) {
  return serialize(
      QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"error", error}});
}

} // namespace

const QStringList &McpServer::modernVersions() {
  static const QStringList kVersions = {QStringLiteral("2026-07-28")};
  return kVersions;
}

const QStringList &McpServer::legacyVersions() {
  static const QStringList kVersions = {
      QStringLiteral("2025-11-25"), QStringLiteral("2025-06-18"),
      QStringLiteral("2025-03-26"), QStringLiteral("2024-11-05")};
  return kVersions;
}

McpServer::McpServer(QString serverVersion, QObject *parent)
    : QObject(parent), m_serverVersion(std::move(serverVersion)) {}

void McpServer::addTool(Tool tool) { m_tools.append(std::move(tool)); }

QJsonObject McpServer::serverInfo() const {
  return {{"name", "hodeia"},
          {"title", "HodeIA"},
          {"version", m_serverVersion}};
}

QJsonObject McpServer::capabilities() const {
  return {{"tools", QJsonObject{{"listChanged", false}}}};
}

QJsonObject McpServer::toolsListPayload() const {
  QJsonArray tools;
  for (const Tool &t : m_tools)
    tools.append(QJsonObject{{"name", t.name},
                             {"title", t.title},
                             {"description", t.description},
                             {"inputSchema", t.inputSchema}});
  return {{"tools", tools}};
}

std::expected<QJsonObject, QJsonObject>
McpServer::callTool(const QJsonObject &params) {
  const QString name = params.value("name").toString();
  for (const Tool &t : m_tools) {
    if (t.name != name)
      continue;
    const auto out = t.call(params.value("arguments").toObject());
    const QString text = out ? *out : out.error();
    return QJsonObject{
        {"content",
         QJsonArray{QJsonObject{{"type", "text"}, {"text", text}}}},
        {"isError", !out.has_value()}};
  }
  return std::unexpected(
      errorObject(kInvalidParams, QString("Unknown tool: %1").arg(name)));
}

QByteArray McpServer::handleMessage(const QByteArray &message) {
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(message, &parseError);
  if (parseError.error != QJsonParseError::NoError)
    return errorResponse(QJsonValue::Null,
                         errorObject(kParseError, "Parse error"));
  if (!doc.isObject())
    return errorResponse(QJsonValue::Null,
                         errorObject(kInvalidRequest, "Invalid request"));

  const QJsonObject msg = doc.object();
  const QString method = msg.value("method").toString();
  if (method.isEmpty())
    return {}; // a response to a server request: none sent, ignore
  if (!msg.contains("id"))
    return {}; // notification (initialized, cancelled...): no answer
  const QJsonValue id = msg.value("id");
  const QJsonObject params = msg.value("params").toObject();
  const QJsonObject meta = params.value("_meta").toObject();

  // ── Legacy handshake ──
  if (method == "initialize") {
    const QString requested = params.value("protocolVersion").toString();
    m_legacyVersion = legacyVersions().contains(requested)
                          ? requested
                          : legacyVersions().first();
    qInfo() << "[MCP] initialize — client asked" << requested << "→"
            << m_legacyVersion;
    return resultResponse(id, {{"protocolVersion", m_legacyVersion},
                               {"capabilities", capabilities()},
                               {"serverInfo", serverInfo()},
                               {"instructions", kInstructions}});
  }
  if (method == "ping") // legacy only (removed in 2026-07-28)
    return resultResponse(id, {});

  // ── Modern discovery ──
  if (method == "server/discover") {
    QStringList supported = modernVersions();
    supported += legacyVersions();
    return resultResponse(
        id, {{"resultType", "complete"},
             {"supportedVersions", QJsonArray::fromStringList(supported)},
             {"capabilities", capabilities()},
             {"instructions", kInstructions},
             {"ttlMs", 0},
             {"cacheScope", "public"},
             {"_meta", QJsonObject{{kMetaServerInfo, serverInfo()}}}});
  }

  // A request carrying a protocol version in _meta is modern: check it, and
  // shape the result the modern way.
  const bool modern = meta.contains(kMetaProtocolVersion);
  if (modern) {
    const QString requested = meta.value(kMetaProtocolVersion).toString();
    if (!modernVersions().contains(requested)) {
      QStringList supported = modernVersions();
      supported += legacyVersions();
      return errorResponse(
          id, errorObject(kUnsupportedProtocolVersion,
                          "Unsupported protocol version",
                          QJsonObject{{"supported",
                                       QJsonArray::fromStringList(supported)},
                                      {"requested", requested}}));
    }
  }
  auto finish = [&](QJsonObject result, bool cacheable) {
    if (modern) {
      result["resultType"] = "complete";
      result["_meta"] = QJsonObject{{kMetaServerInfo, serverInfo()}};
      if (cacheable) {
        result["ttlMs"] = 0;
        result["cacheScope"] = "public";
      }
    }
    return resultResponse(id, result);
  };

  if (method == "tools/list")
    return finish(toolsListPayload(), true);
  if (method == "tools/call") {
    auto result = callTool(params);
    if (!result)
      return errorResponse(id, result.error());
    return finish(*result, false);
  }

  return errorResponse(
      id, errorObject(kMethodNotFound,
                      QString("Method not found: %1").arg(method)));
}
