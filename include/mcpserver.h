#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <expected>
#include <functional>

/// Model Context Protocol server: JSON-RPC 2.0 messages in, responses out.
/// No I/O here (see McpStdioTransport), so the protocol is testable as is.
///
/// Dual-era (MCP "versioning" page):
///  - legacy clients (protocol 2025-11-25 and earlier) open with an
///    `initialize` handshake; the negotiated version then applies to the
///    whole stdio process;
///  - modern clients (2026-07-28) send no handshake: every request carries
///    its protocol version and capabilities in params._meta, and
///    `server/discover` advertises what the server supports.
class McpServer : public QObject {
  Q_OBJECT

public:
  /// Modern protocol versions served statelessly (per-request _meta).
  static const QStringList &modernVersions();
  /// Legacy protocol versions served after an `initialize` handshake,
  /// newest first.
  static const QStringList &legacyVersions();

  /// A tool the server exposes.  `call` gets the call's arguments and
  /// returns the text result, or an error message (reported to the model
  /// as a tool error, isError = true).
  struct Tool {
    QString name;
    QString title;
    QString description;
    QJsonObject inputSchema;
    std::function<std::expected<QString, QString>(const QJsonObject &)> call;
  };

  explicit McpServer(QString serverVersion, QObject *parent = nullptr);

  /// Expose a tool (tools/list lists them in the order added).
  void addTool(Tool tool);

  /// Handle one JSON-RPC message (one line of the stdio transport).  Returns
  /// the response to send, or an empty array for a notification or a
  /// client response, which get no answer.
  QByteArray handleMessage(const QByteArray &message);

  /// Protocol version agreed by `initialize` (empty before, or for a client
  /// that only sends modern requests).
  QString legacyVersion() const { return m_legacyVersion; }

private:
  QJsonObject serverInfo() const;
  QJsonObject capabilities() const;
  QJsonObject toolsListPayload() const;
  /// The result of tools/call, or a JSON-RPC error object.
  std::expected<QJsonObject, QJsonObject> callTool(const QJsonObject &params);

  QString m_serverVersion;
  QList<Tool> m_tools;
  QString m_legacyVersion;
};
