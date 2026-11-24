#pragma once

#include "agentconnection.h"
#include "agentprocess.h"

#include <QHash>
#include <QJsonValue>
#include <QTimer>
#include <functional>

namespace Agents {

/// Codex driven directly through its own protocol, `codex app-server`
/// (JSON-RPC over stdio, the protocol of Codex's IDE extensions), without
/// the codex-acp adapter: the Codex the user installed and logged into is
/// the one that answers, with its own models.
///
/// One process and one thread per session: initialize, thread/start (or
/// thread/resume), then turn/start per prompt; the answer comes as item
/// notifications and the turn ends with turn/completed.  Cancel:
/// turn/interrupt, then the process is killed if the turn does not end
/// within a few seconds.
///
/// Codex runs read-only and never asks for approval: HodeIA's MCP tools
/// listed in SessionOptions::allowedTools are pre-approved in the thread's
/// config, everything else is refused.  The user's own MCP servers are
/// disabled for the thread.  Classic mode (no tools) also turns off the
/// built-in tools Codex lets a client turn off (shell, patches, apps,
/// skills...; web search unless SessionOptions::web); some remain, decided
/// by the model, inside the read-only sandbox.
class CodexConnection : public Connection {
  Q_OBJECT

public:
  CodexConnection(const Profile &profile, QObject *parent = nullptr);
  ~CodexConnection() override;

  void open(const SessionOptions &options) override;
  void prompt(const Prompt &prompt) override;
  void cancel() override;
  void close() override;
  QString sessionId() const override { return m_threadId; }
  bool resumed() const override { return m_resumed; }

  /// Start Codex and ask for its models (not hidden ones): modelsListed()
  /// or failed().  No thread is opened.
  void requestModels();

  /// The thread/start (or thread/resume) parameters of `options`; the
  /// user's MCP servers in `userMcpServers` are disabled (tests).
  static QJsonObject threadParams(const SessionOptions &options,
                                  const QStringList &userMcpServers);
  /// The turn/start input items of a prompt (tests).
  static QJsonArray inputItems(const Prompt &prompt);

signals:
  void modelsListed(const QStringList &models);

private:
  using Callback = std::function<void(const QJsonValue &result, const QJsonObject *error)>;
  void start(std::function<void()> ready);
  void call(const QString &method, const QJsonObject &params, Callback done);
  void handle(const QJsonObject &message);
  void handleNotification(const QString &method, const QJsonObject &params);
  void answerServerRequest(const QJsonObject &message);
  void startThread(const QStringList &userMcpServers, bool resume);
  void fetchModels(const QString &cursor, QStringList models);
  void addAnswer(const QString &itemId, const QString &text, bool delta);
  void finishTurn(const TurnResult &result);
  void fail(const QString &error);

  Profile m_profile;
  SessionOptions m_options;
  AgentProcess m_process;
  qint64 m_nextId = 1;
  QHash<qint64, Callback> m_pending;
  QString m_threadId;
  QString m_turnId;
  QString m_answer;
  QString m_lastItem;  ///< the agent message being written
  QString m_turnError; ///< an error notification of this turn
  bool m_inTurn = false;
  bool m_cancelRequested = false;
  bool m_failed = false;
  bool m_resumed = false;
  QTimer m_killTimer;
};

class CodexConnector : public Connector {
public:
  QString id() const override { return QStringLiteral("codex"); }
  Connection *create(const Profile &profile, QObject *parent) const override;
  /// "default" (Codex's own choice), then the models of the account.
  std::expected<QStringList, QString> listModels(const Profile &profile) const override;
  bool supportsMcp() const override { return true; }
};

} // namespace Agents
