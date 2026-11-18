#pragma once

#include <QByteArray>
#include <QObject>

class McpServer;
class QThread;

/// MCP stdio transport: newline-delimited JSON-RPC messages on stdin, the
/// responses on stdout, nothing else on stdout (logs go to stderr).
///
/// stdin is read by a blocking reader thread; each line is handed to the
/// server on this object's thread, which writes the response.  On Windows
/// the inherited pipe handles are used directly (ReadFile / WriteFile): a
/// GUI-subsystem executable has no console, and the C runtime's text mode
/// would rewrite line endings.
class McpStdioTransport : public QObject {
  Q_OBJECT

public:
  explicit McpStdioTransport(McpServer &server, QObject *parent = nullptr);
  ~McpStdioTransport() override;

  /// Start reading stdin.
  void start();

signals:
  /// stdin was closed (the client is gone): time to quit.
  void closed();

private slots:
  void onLine(const QByteArray &line);

private:
  void writeLine(const QByteArray &line);

  McpServer &m_server;
  QThread *m_reader = nullptr;
};
