#pragma once

#include "agentconnection.h"
#include "agentprocess.h"

#include <QTimer>

namespace Agents {

/// Claude Code driven directly through its CLI, without the ACP adapter (no
/// Node.js, no npm): `claude -p --input-format stream-json --output-format
/// stream-json`.  One process per session; each prompt is one user message
/// on stdin, the answer ends with a "result" message.  Claude Code uses the
/// user's own login.
///
/// Classic mode: `--tools "" --setting-sources ""` (no tool, none of the
/// user's settings) and HodeIA's `--system-prompt`: about 650 tokens of
/// overhead.  Cancel: an "interrupt" control request, then the process is
/// killed if the turn does not end within a few seconds.
class ClaudeConnection : public Connection {
  Q_OBJECT

public:
  ClaudeConnection(const Profile &profile, QObject *parent = nullptr);
  ~ClaudeConnection() override;

  void open(const SessionOptions &options) override;
  void prompt(const Prompt &prompt) override;
  void cancel() override;
  void close() override;
  QString sessionId() const override { return m_sessionId; }
  bool resumed() const override { return m_resumed; }

  /// The command-line arguments added for `options` (tests).
  static QStringList arguments(const SessionOptions &options);
  /// The stream-json user message of a prompt (tests).
  static QJsonObject userMessage(const Prompt &prompt);

private:
  void handle(const QJsonObject &message);
  void finishTurn(const TurnResult &result);

  Profile m_profile;
  AgentProcess m_process;
  QString m_sessionId;
  QString m_answer;
  QString m_notice; ///< e.g. a rate limit, for the error message
  bool m_inTurn = false;
  bool m_cancelled = false;
  bool m_closed = false;
  bool m_resumed = false;
  QTimer m_killTimer;
  int m_controlId = 0;
};

class ClaudeConnector : public Connector {
public:
  QString id() const override { return QStringLiteral("claude"); }
  Connection *create(const Profile &profile, QObject *parent) const override;
  /// The aliases Claude Code accepts (profile "models"): the CLI cannot list
  /// the models of the account.
  std::expected<QStringList, QString> listModels(const Profile &profile) const override;
  bool supportsMcp() const override { return true; }
};

} // namespace Agents
