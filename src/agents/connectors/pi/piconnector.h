#pragma once

#include "agentconnection.h"
#include "agentprocess.h"

#include <QHash>
#include <functional>

namespace Agents {

/// Pi (pi-coding-agent) driven through its RPC mode: `pi --mode rpc`, JSON
/// commands on stdin, responses and session events on stdout (documented in
/// Pi's docs/rpc.md).  One process per session; a turn ends with
/// "agent_settled".  Pi uses the providers configured by the user.
///
/// Classic mode: no tool, extension, skill, prompt template or context file
/// (AGENTS.md, CLAUDE.md), HodeIA's `--system-prompt`: about 100 tokens of
/// overhead.  Models are "provider/id"; cancel sends "abort".
class PiConnection : public Connection {
  Q_OBJECT

public:
  PiConnection(const Profile &profile, QObject *parent = nullptr);
  ~PiConnection() override;

  void open(const SessionOptions &options) override;
  void prompt(const Prompt &prompt) override;
  void cancel() override;
  void close() override;
  QString sessionId() const override { return m_sessionId; }

  /// The models Pi offers, as "provider/id" (after opened()).
  QStringList models() const { return m_models; }

  /// The command-line arguments added for `options` (tests).
  static QStringList arguments(const SessionOptions &options);

private:
  using Callback = std::function<void(const QJsonObject &response)>;
  void command(const QJsonObject &cmd, Callback done = {});
  void handle(const QJsonObject &message);
  void finishTurn(const TurnResult &result);
  void fail(const QString &error);

  Profile m_profile;
  AgentProcess m_process;
  int m_nextId = 1;
  QHash<QString, Callback> m_pending;
  QString m_sessionId;
  QStringList m_models;
  QString m_answer;
  QString m_error;
  bool m_inTurn = false;
  bool m_closed = false;
};

class PiConnector : public Connector {
public:
  QString id() const override { return QStringLiteral("pi"); }
  Connection *create(const Profile &profile, QObject *parent) const override;
  std::expected<QStringList, QString> listModels(const Profile &profile) const override;
};

} // namespace Agents
