#include "acpconnector.h"

#include "agentsync.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <memory>

namespace Agents {

namespace {

constexpr int kAuthRequired = -32000;
constexpr int kMethodNotFound = -32601;


QJsonObject initializeParams() {
  return {{"protocolVersion", 1},
          {"clientCapabilities",
           QJsonObject{{"fs", QJsonObject{{"readTextFile", false},
                                          {"writeTextFile", false}}},
                       {"terminal", false}}},
          {"clientInfo", QJsonObject{{"name", "HodeIA"},
                                     {"version", QCoreApplication::applicationVersion()}}}};
}

TurnResult::Stop stopOf(const QString &reason) {
  if (reason == "cancelled")
    return TurnResult::Stop::Cancelled;
  if (reason == "refusal")
    return TurnResult::Stop::Refusal;
  if (reason == "max_tokens" || reason == "max_turn_requests")
    return TurnResult::Stop::MaxTokens;
  return TurnResult::Stop::EndTurn;
}

} // namespace

AcpConnection::AcpConnection(const Profile &profile, QObject *parent)
    : Connection(parent), m_profile(profile) {
  connect(&m_process, &AgentProcess::message, this, &AcpConnection::handle);
  connect(&m_process, &AgentProcess::exited, this, [this](const QString &reason) {
    QString detail = reason;
    if (!m_process.stderrTail().isEmpty())
      detail += "\n" + m_process.stderrTail().right(800);
    fail(detail);
  });
}

AcpConnection::~AcpConnection() { close(); }

QJsonObject AcpConnection::sessionParams(const SessionOptions &options,
                                         const QJsonObject &sessionMeta,
                                         bool systemPromptSupported) {
  QJsonArray servers;
  for (const McpServer &s : options.mcpServers) {
    QJsonArray env;
    for (auto it = s.env.cbegin(); it != s.env.cend(); ++it)
      env.append(QJsonObject{{"name", it.key()}, {"value", it.value()}});
    servers.append(QJsonObject{{"name", s.name},
                               {"command", s.command},
                               {"args", QJsonArray::fromStringList(s.args)},
                               {"env", env}});
  }
  QJsonObject params{{"cwd", QDir::toNativeSeparators(QDir(options.workDir).absolutePath())},
                     {"mcpServers", servers}};
  if (!options.tools && !sessionMeta.isEmpty())
    params["_meta"] = sessionMeta;
  if (systemPromptSupported && !options.systemPrompt.isEmpty())
    params["systemPrompt"] = QJsonObject{
        {"mode", "override"},
        {"content", QJsonArray{QJsonObject{{"type", "text"},
                                           {"text", options.systemPrompt}}}}};
  return params;
}

QJsonArray AcpConnection::promptBlocks(const Prompt &prompt) {
  QJsonArray blocks;
  for (const PromptPart &part : prompt)
    blocks.append(part.isImage()
                      ? QJsonObject{{"type", "image"},
                                    {"mimeType", part.mimeType},
                                    {"data", QString::fromLatin1(part.base64)}}
                      : QJsonObject{{"type", "text"}, {"text", part.text}});
  return blocks;
}

void AcpConnection::call(const QString &method, const QJsonObject &params,
                         Callback done) {
  const qint64 id = m_nextId++;
  m_pending.insert(id, std::move(done));
  m_process.write({{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}});
}

void AcpConnection::notify(const QString &method, const QJsonObject &params) {
  m_process.write({{"jsonrpc", "2.0"}, {"method", method}, {"params", params}});
}

QString AcpConnection::describe(const QJsonObject &error) const {
  const QString message = error.value("message").toString();
  if (error.value("code").toInt() == kAuthRequired ||
      message.contains("auth", Qt::CaseInsensitive) ||
      message.contains("login", Qt::CaseInsensitive)) {
    QString text = QCoreApplication::translate("Agents::AcpConnection", "%1 is not logged in.").arg(m_profile.name);
    if (!m_profile.loginHint.isEmpty())
      text += " " + QCoreApplication::translate("Agents::AcpConnection", "Log in with: %1").arg(m_profile.loginHint);
    return text + "\n(" + message + ")";
  }
  return message;
}

void AcpConnection::fail(const QString &error) {
  if (m_failed)
    return;
  m_failed = true;
  const auto pending = std::exchange(m_pending, {});
  const QJsonObject e{{"code", 0}, {"message", error}};
  for (const Callback &done : pending)
    done({}, &e);
  if (m_inTurn) {
    m_inTurn = false;
    emit turnFinished({TurnResult::Stop::Error, m_answer, error});
  }
  emit failed(error);
}

void AcpConnection::open(const SessionOptions &options) {
  m_options = options;
  m_failed = false;
  if (!m_process.start(m_profile.command, {}, options.workDir, m_profile.env)) {
    const QString error = startFailureMessage(m_profile, m_process.lastError());
    fail(error);
    return;
  }
  call("initialize", initializeParams(), [this](const QJsonValue &init, const QJsonObject *e) {
    if (e) {
      fail(QCoreApplication::translate("Agents::AcpConnection", "The agent did not start: %1").arg(describe(*e)));
      return;
    }
    const bool overrideOk = init.toObject()
                                .value("agentCapabilities").toObject()
                                .value("sessionCapabilities").toObject()
                                .value("systemPrompt").toObject()
                                .value("mode").toObject()
                                .contains("override");
    if (!overrideOk)
      m_instructions = m_options.systemPrompt;
    const bool canResume = init.toObject()
                               .value("agentCapabilities").toObject()
                               .value("sessionCapabilities").toObject()
                               .contains("resume");
    if (m_options.resumeSessionId.isEmpty() || !canResume) {
      newSession(overrideOk);
      return;
    }
    QJsonObject params = sessionParams(m_options, m_profile.options.value("sessionMeta").toObject(),
                                       overrideOk);
    params.remove("systemPrompt");
    params["sessionId"] = m_options.resumeSessionId;
    call("session/resume", params,
         [this, overrideOk](const QJsonValue &session, const QJsonObject *e) {
           if (e) {
             qInfo() << "[ACP] session not resumed:" << e->value("message").toString();
             newSession(overrideOk);
             return;
           }
           m_sessionId = m_options.resumeSessionId;
           m_resumed = true;
           m_instructions.clear(); // already given in that session
           selectModel(session.toObject());
         });
  });
}

void AcpConnection::newSession(bool overrideOk) {
  call("session/new",
       sessionParams(m_options, m_profile.options.value("sessionMeta").toObject(), overrideOk),
       [this](const QJsonValue &session, const QJsonObject *e) {
         if (e) {
           fail(describe(*e));
           return;
         }
         m_sessionId = session.toObject().value("sessionId").toString();
         selectModel(session.toObject());
       });
}

void AcpConnection::selectModel(const QJsonObject &session) {
  // The models offered: a "model" config option, or the `models` field.
  QString viaConfig;
  for (const QJsonValue &v : session.value("configOptions").toArray()) {
    const QJsonObject o = v.toObject();
    if (o.value("id").toString() != "model")
      continue;
    viaConfig = o.value("id").toString();
    for (const QJsonValue &option : o.value("options").toArray())
      m_models.append(option.toObject().value("value").toString());
  }
  const bool viaModels = viaConfig.isEmpty() && session.contains("models");
  if (viaModels)
    for (const QJsonValue &m : session.value("models").toObject().value("availableModels").toArray())
      m_models.append(m.toObject().value("modelId").toString());

  const QString model = m_options.model;
  if (model.isEmpty() || model == "default" || (!viaModels && viaConfig.isEmpty())) {
    emit opened();
    return;
  }
  const auto done = [this, model](const QJsonValue &, const QJsonObject *e) {
    if (e)
      fail(QCoreApplication::translate("Agents::AcpConnection", "The agent refused the model \"%1\": %2").arg(model, describe(*e)));
    else
      emit opened();
  };
  if (viaModels)
    call("session/set_model", {{"sessionId", m_sessionId}, {"modelId", model}}, done);
  else
    call("session/set_config_option",
         {{"sessionId", m_sessionId}, {"configId", "model"}, {"value", model}}, done);
}

void AcpConnection::prompt(const Prompt &prompt) {
  if (m_failed || m_sessionId.isEmpty()) {
    emit turnFinished({TurnResult::Stop::Error, {}, QCoreApplication::translate("Agents::AcpConnection", "No open session.")});
    return;
  }
  Prompt full = prompt;
  if (!m_instructions.isEmpty()) {
    full.prepend(PromptPart::textPart(m_instructions + "\n\n---\n\n"));
    m_instructions.clear();
  }
  m_answer.clear();
  m_inTurn = true;
  call("session/prompt", {{"sessionId", m_sessionId}, {"prompt", promptBlocks(full)}},
       [this](const QJsonValue &result, const QJsonObject *e) {
         if (!m_inTurn)
           return;
         m_inTurn = false;
         if (e) {
           emit turnFinished({TurnResult::Stop::Error, m_answer, describe(*e)});
           return;
         }
         const QString reason = result.toObject().value("stopReason").toString();
         TurnResult r{stopOf(reason), m_answer, {}};
         if (r.stop == TurnResult::Stop::Refusal)
           r.error = QCoreApplication::translate("Agents::AcpConnection", "The agent declined the request.");
         emit turnFinished(r);
       });
}

void AcpConnection::cancel() {
  if (m_inTurn && !m_sessionId.isEmpty())
    notify("session/cancel", {{"sessionId", m_sessionId}});
  else if (m_sessionId.isEmpty())
    fail(QCoreApplication::translate("Agents::AcpConnection", "Cancelled."));
}

void AcpConnection::close() {
  m_failed = true; // no signal from here on
  m_pending.clear();
  m_process.stop();
}

void AcpConnection::handle(const QJsonObject &msg) {
  const QString method = msg.value("method").toString();
  if (method.isEmpty()) { // a response
    Callback done = m_pending.take(msg.value("id").toInteger(-1));
    if (!done)
      return;
    if (msg.contains("error")) {
      const QJsonObject e = msg.value("error").toObject();
      done({}, &e);
    } else {
      done(msg.value("result"), nullptr);
    }
    return;
  }

  const QJsonObject params = msg.value("params").toObject();
  if (method == "session/update") {
    const QJsonObject u = params.value("update").toObject();
    const QString kind = u.value("sessionUpdate").toString();
    if (kind == "agent_message_chunk" || kind == "agent_thought_chunk") {
      const QJsonObject content = u.value("content").toObject();
      if (content.value("type").toString() != "text")
        return;
      const QString text = content.value("text").toString();
      const bool answer = kind == "agent_message_chunk";
      if (answer)
        m_answer += text;
      emit agentEvent({answer ? Event::Kind::Text : Event::Kind::Thought, text, {}, {}});
    } else if (kind == "tool_call" || kind == "tool_call_update") {
      emit agentEvent({Event::Kind::ToolCall, u.value("title").toString(),
                  u.value("toolCallId").toString(), u.value("status").toString()});
    }
  } else if (method == "session/request_permission" && msg.contains("id")) {
    // Only HodeIA's own tools are allowed (mode agentique); the rest, and
    // everything in classic mode, is refused.
    const QString optionId = permissionAnswer(params, m_options.allowedTools);
    const QJsonObject outcome =
        optionId.isEmpty() ? QJsonObject{{"outcome", "cancelled"}}
                           : QJsonObject{{"outcome", "selected"}, {"optionId", optionId}};
    qInfo() << "[ACP] permission" << optionId << "for"
            << params.value("toolCall").toObject().value("title").toString();
    m_process.write({{"jsonrpc", "2.0"}, {"id", msg.value("id")},
                     {"result", QJsonObject{{"outcome", outcome}}}});
  } else if (msg.contains("id")) {
    m_process.write({{"jsonrpc", "2.0"}, {"id", msg.value("id")},
                     {"error", QJsonObject{{"code", kMethodNotFound},
                                           {"message", "Method not supported: " + method}}}});
  }
}

QString AcpConnection::permissionAnswer(const QJsonObject &params,
                                       const QStringList &allowedTools) {
  // Agents name MCP tools differently ("mcp__hodeia__search_library",
  // "search_library (hodeia MCP Server)"...): the tool call is searched for
  // the server and the tool name.
  const QJsonObject call = params.value("toolCall").toObject();
  const QString seen = call.value("title").toString() + " " +
                       QJsonDocument(call.value("rawInput").toObject()).toJson() + " " +
                       call.value("name").toString() + " " +
                       params.value("_meta").toVariant().toString();
  bool allowed = false;
  for (const QString &tool : allowedTools) {
    const QString server = tool.section('/', 0, 0), name = tool.section('/', 1);
    if (seen.contains(name) && (seen.contains(server, Qt::CaseInsensitive)))
      allowed = true;
  }
  QString wanted = allowed ? QStringLiteral("allow_once") : QStringLiteral("reject_once");
  for (const QJsonValue &o : params.value("options").toArray())
    if (o.toObject().value("kind").toString() == wanted)
      return o.toObject().value("optionId").toString();
  return {};
}

Connection *AcpConnector::create(const Profile &profile, QObject *parent) const {
  return new AcpConnection(profile, parent);
}

std::expected<QStringList, QString> AcpConnector::listModels(const Profile &profile) const {
  if (!profile.models.isEmpty())
    return profile.models;
  AcpConnection connection(profile);
  SessionOptions options;
  options.workDir = QDir::tempPath();
  if (auto r = openSync(connection, options, kOpenTimeoutMs); !r)
    return std::unexpected(r.error());
  QStringList models = connection.models();
  connection.close();
  if (models.isEmpty())
    models.append(QStringLiteral("default"));
  return models;
}

} // namespace Agents

std::unique_ptr<Agents::Connector> hodeia_connector_acp() {
  return std::make_unique<Agents::AcpConnector>();
}
