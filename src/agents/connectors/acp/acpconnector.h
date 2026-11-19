#pragma once

#include "agentconnection.h"
#include "agentprocess.h"

#include <QHash>
#include <QJsonValue>
#include <functional>

namespace Agents {

/// Agent Client Protocol, version 1 (https://agentclientprotocol.com): the
/// connector of every agent that speaks ACP (Qwen Code, Gemini CLI,
/// Antigravity, the Claude ACP adapter...).  JSON-RPC 2.0 over stdio.
///
/// Profile options:
///   "sessionMeta": object sent as `_meta` of session/new in classic mode
///                  (e.g. Claude's {"claudeCode": {"options": {"tools": []}}}).
///
/// The model is chosen by session/set_config_option when the agent offers a
/// "model" config option, else by session/set_model when it lists `models`.
/// When the agent cannot replace its system prompt, HodeIA's goes at the top
/// of the first prompt.  The client offers no file system and no terminal;
/// permission requests are refused.
class AcpConnection : public Connection {
  Q_OBJECT

public:
  AcpConnection(const Profile &profile, QObject *parent = nullptr);
  ~AcpConnection() override;

  void open(const SessionOptions &options) override;
  void prompt(const Prompt &prompt) override;
  void cancel() override;
  void close() override;
  QString sessionId() const override { return m_sessionId; }
  bool resumed() const override { return m_resumed; }

  /// The models the session offers (after opened()).
  QStringList models() const { return m_models; }

  /// session/new parameters (tests).
  static QJsonObject sessionParams(const SessionOptions &options,
                                   const QJsonObject &sessionMeta,
                                   bool systemPromptSupported);
  /// The ACP content blocks of a prompt.
  static QJsonArray promptBlocks(const Prompt &prompt);
  /// The option to answer a permission request with: "allow_once" when the
  /// tool is one of `allowedTools` ("server/tool"), else "reject_once";
  /// empty for "cancelled" (tests).
  static QString permissionAnswer(const QJsonObject &params, const QStringList &allowedTools);

private:
  using Callback = std::function<void(const QJsonValue &result, const QJsonObject *error)>;
  void call(const QString &method, const QJsonObject &params, Callback done);
  void notify(const QString &method, const QJsonObject &params);
  void handle(const QJsonObject &message);
  void fail(const QString &error);
  void selectModel(const QJsonObject &session);
  void newSession(bool overrideOk);
  QString describe(const QJsonObject &error) const;

  Profile m_profile;
  SessionOptions m_options;
  AgentProcess m_process;
  qint64 m_nextId = 1;
  QHash<qint64, Callback> m_pending;
  QString m_sessionId;
  QStringList m_models;
  QString m_instructions; ///< system prompt to put in the first prompt
  QString m_answer;
  bool m_inTurn = false;
  bool m_failed = false;
  bool m_resumed = false;
};

class AcpConnector : public Connector {
public:
  QString id() const override { return QStringLiteral("acp"); }
  Connection *create(const Profile &profile, QObject *parent) const override;
  std::expected<QStringList, QString> listModels(const Profile &profile) const override;
  bool supportsMcp() const override { return true; }
};

} // namespace Agents
