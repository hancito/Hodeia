#include "acpconnector.h"

#include "agentsync.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QProcessEnvironment>
#include <QSet>
#include <QTemporaryDir>
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

QJsonArray AcpConnection::promptBlocks(const Prompt &prompt, int maxImages) {
  QJsonArray blocks;
  int images = 0;
  for (const PromptPart &part : prompt) {
    if (!part.isImage()) {
      blocks.append(QJsonObject{{"type", "text"}, {"text", part.text}});
      continue;
    }
    if (maxImages >= 0 && images >= maxImages)
      continue; // over the agent's limit: the page's text still goes
    ++images;
    blocks.append(QJsonObject{{"type", "image"},
                              {"mimeType", part.mimeType},
                              {"data", QString::fromLatin1(part.base64)}});
  }
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
  const QString base = error.value("message").toString();
  // The cause often hides in "data" behind a generic message ("Internal
  // error"): {"message": "..."} or, from the Python ACP SDK, {"details":
  // "..."}, possibly the provider's own JSON error.
  const QJsonValue data = error.value("data");
  QString detail;
  if (data.isString()) {
    detail = data.toString();
  } else {
    const QJsonObject d = data.toObject();
    detail = d.value("message").toString();
    if (detail.isEmpty())
      detail = d.value("details").toString();
  }
  if (const QJsonDocument inner = QJsonDocument::fromJson(detail.toUtf8()); inner.isObject()) {
    const QJsonObject o = inner.object();
    const QJsonValue e = o.value("error");
    const QString m = e.isObject() ? e.toObject().value("message").toString()
                                   : (e.isString() ? e.toString() : o.value("message").toString());
    if (!m.isEmpty())
      detail = m;
  }
  QString message = base;
  if (!detail.isEmpty() && detail != base)
    message = base.isEmpty() ? detail : base + ": " + detail;
  // Judged on the agent's own message: a provider detail may well mention
  // an "account" or "auth" without the user being logged out.
  if (error.value("code").toInt() == kAuthRequired ||
      base.contains("auth", Qt::CaseInsensitive) ||
      base.contains("login", Qt::CaseInsensitive)) {
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

QJsonValue AcpConnection::expand(const QJsonValue &value, const Profile &profile,
                                 const QString &workDir) {
  if (value.isArray()) {
    QJsonArray out;
    for (const QJsonValue &v : value.toArray())
      out.append(expand(v, profile, workDir));
    return out;
  }
  if (value.isObject()) {
    QJsonObject out;
    const QJsonObject o = value.toObject();
    for (auto it = o.begin(); it != o.end(); ++it)
      out.insert(expand(it.key(), profile, workDir).toString(), expand(it.value(), profile, workDir));
    return out;
  }
  if (!value.isString())
    return value;
  // A shell command line (hooks run through sh -c or cmd /c).
  QStringList guard;
  for (const QString &part : profile.guardCommand)
    guard.append(part.contains(' ') ? '"' + part + '"' : part);
  const QStringList command = AgentProcess::splitCommand(profile.command);
  QString s = value.toString();
  s.replace("${stateDir}", QDir::toNativeSeparators(profile.stateDir));
  s.replace("${workDir}", QDir::toNativeSeparators(workDir));
  s.replace("${program}", command.isEmpty() ? QString() : command.first());
  s.replace("${guard}", guard.join(' '));
  return s;
}

namespace {

/// Setups done in this HodeIA session (all connections, all threads).
QMutex s_setupMutex;
QSet<QString> s_setupsDone;

QString setupKey(const Profile &profile) {
  return profile.id + '\n' + profile.command + '\n' +
         QJsonDocument(profile.options.value("setup").toArray()).toJson(QJsonDocument::Compact);
}

} // namespace

void AcpConnection::open(const SessionOptions &options) {
  m_options = options;
  m_failed = false;
  prepare();
}

void AcpConnection::prepare() {
  // The files the profile asks for (settings, hooks), then its setup.
  const QJsonObject files = m_profile.options.value("files").toObject();
  for (auto it = files.begin(); it != files.end(); ++it) {
    const QString path = expand(it.key(), m_profile, m_options.workDir).toString();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        file.write(QJsonDocument(expand(it.value(), m_profile, m_options.workDir).toObject())
                       .toJson()) < 0) {
      fail(QCoreApplication::translate("Agents::AcpConnection", "Could not write %1: %2")
               .arg(QDir::toNativeSeparators(path), file.errorString()));
      return;
    }
  }
  {
    QMutexLocker lock(&s_setupMutex);
    if (s_setupsDone.contains(setupKey(m_profile))) {
      lock.unlock();
      startAgent();
      return;
    }
  }
  runSetup(0);
}

void AcpConnection::runSetup(int step) {
  const QJsonArray steps = m_profile.options.value("setup").toArray();
  if (step >= steps.size()) {
    QMutexLocker lock(&s_setupMutex);
    s_setupsDone.insert(setupKey(m_profile));
    lock.unlock();
    startAgent();
    return;
  }
  const QJsonObject spec = expand(steps.at(step), m_profile, m_options.workDir).toObject();
  // "unless" first, when there is one; then "run".
  const bool checking = spec.contains("unless") && !m_setupChecked;
  const QStringList argv = spec.value(checking ? "unless" : "run").toVariant().toStringList();
  if (argv.isEmpty()) {
    m_setupChecked = false;
    runSetup(step + 1);
    return;
  }
  m_setup = std::make_unique<QProcess>();
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  for (auto it = m_profile.env.cbegin(); it != m_profile.env.cend(); ++it)
    env.insert(it.key(), expand(it.value(), m_profile, m_options.workDir).toString());
  m_setup->setProcessEnvironment(env);
  m_setup->setProcessChannelMode(QProcess::MergedChannels);
  QProcess *process = m_setup.get();
  connect(process, &QProcess::finished, this, [this, process, step, checking, argv](int code, QProcess::ExitStatus status) {
    const bool ok = status == QProcess::NormalExit && code == 0;
    const QString output = QString::fromLocal8Bit(process->readAll()).trimmed().right(600);
    qInfo() << "[ACP] setup" << argv << "->" << code;
    if (checking) {
      m_setupChecked = !ok; // "unless" failed: now "run"
      runSetup(ok ? step + 1 : step);
    } else if (!ok) {
      fail(QCoreApplication::translate("Agents::AcpConnection", "Preparing %1 failed (%2): %3")
               .arg(m_profile.name, argv.join(' '), output));
    } else {
      m_setupChecked = false;
      runSetup(step + 1);
    }
  }, Qt::QueuedConnection);
  connect(process, &QProcess::errorOccurred, this, [this, process, argv](QProcess::ProcessError e) {
    if (e != QProcess::FailedToStart)
      return;
    fail(startFailureMessage(m_profile, process->errorString()));
  }, Qt::QueuedConnection);
  process->start(argv.first(), argv.mid(1));
}

void AcpConnection::startAgent() {
  if (m_failed)
    return;
  QMap<QString, QString> env;
  for (auto it = m_profile.env.cbegin(); it != m_profile.env.cend(); ++it)
    env.insert(it.key(), expand(it.value(), m_profile, m_options.workDir).toString());
  const SessionOptions &options = m_options;
  if (!m_process.start(m_profile.command, {}, options.workDir, env)) {
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
  call("session/prompt", {{"sessionId", m_sessionId}, {"prompt", promptBlocks(full, m_profile.options.value("maxImages").toInt(-1))}},
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
  if (m_setup) {
    m_setup->disconnect(this);
    m_setup->kill();
    m_setup->waitForFinished(2000);
    m_setup.reset();
  }
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
    const QString optionId = permissionAnswer(params, m_options.allowedTools, m_options.web);
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
                                       const QStringList &allowedTools, bool web) {
  // Agents name MCP tools differently ("mcp__hodeia__search_library",
  // "search_library (hodeia MCP Server)"...): the tool call is searched for
  // the server and the tool name.
  const QJsonObject call = params.value("toolCall").toObject();
  const QString seen = call.value("title").toString() + " " +
                       QJsonDocument(call.value("rawInput").toObject()).toJson() + " " +
                       call.value("name").toString() + " " +
                       params.value("_meta").toVariant().toString();
  bool allowed = web && call.value("kind").toString() == "fetch";
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
  const QTemporaryDir dir; // the profile's files may go in the work folder
  options.workDir = dir.isValid() ? dir.path() : QDir::tempPath();
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
