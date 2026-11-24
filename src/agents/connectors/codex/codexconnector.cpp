#include "codexconnector.h"

#include "agentsync.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <memory>

namespace Agents {

namespace {

constexpr int kMethodNotFound = -32601;
/// After turn/interrupt, the time left to the turn to end before the kill.
constexpr int kInterruptGraceMs = 5000;

QString tr(const char *text) {
  return QCoreApplication::translate("Agents::CodexConnection", text);
}

/// The built-in features turned off in classic mode.  Codex ignores the
/// names it does not know (any more), so the list may outlive a version.
QJsonObject classicFeatures() {
  QJsonObject f;
  for (const char *name : {"apps", "shell_tool", "image_generation", "browser_use",
                           "computer_use", "multi_agent", "multi_agent_v2", "code_mode_host",
                           "goals", "plugins", "skill_search", "sleep_tool", "tool_suggest",
                           "hooks", "memories"})
    f.insert(name, false);
  return f;
}

/// A JSON-RPC error's message, with the detail Codex puts in "data".
QString errorText(const QJsonObject &error) {
  QString message = error.value("message").toString();
  const QJsonValue data = error.value("data");
  const QString detail = data.isString() ? data.toString() : data.toObject().value("message").toString();
  if (!detail.isEmpty() && detail != message)
    message = message.isEmpty() ? detail : message + ": " + detail;
  return message;
}

/// A turn error ({message, additionalDetails}) for the user.
QString turnErrorText(const QJsonObject &error) {
  QString message = error.value("message").toString();
  const QString details = error.value("additionalDetails").toString();
  if (!details.isEmpty() && details != message)
    message += "\n" + details;
  return message;
}

} // namespace

CodexConnection::CodexConnection(const Profile &profile, QObject *parent)
    : Connection(parent), m_profile(profile) {
  m_killTimer.setSingleShot(true);
  connect(&m_killTimer, &QTimer::timeout, this, [this]() {
    qWarning() << "[Codex] the turn did not stop: killing the process";
    m_process.kill();
    finishTurn({TurnResult::Stop::Cancelled, m_answer, {}});
    fail(tr("The agent was stopped."));
  });
  connect(&m_process, &AgentProcess::message, this, &CodexConnection::handle);
  connect(&m_process, &AgentProcess::exited, this, [this](const QString &reason) {
    QString detail = reason;
    if (!m_process.stderrTail().isEmpty())
      detail += "\n" + m_process.stderrTail().right(800);
    fail(detail);
  });
}

CodexConnection::~CodexConnection() { close(); }

QJsonObject CodexConnection::threadParams(const SessionOptions &options,
                                          const QStringList &userMcpServers) {
  QJsonObject config;
  if (!options.tools) {
    config["features"] = classicFeatures();
    config["web_search"] = options.web ? "live" : "disabled";
    config["include_apply_patch_tool"] = false;
    config["skills"] = QJsonObject{{"bundled", QJsonObject{{"enabled", false}}}};
  }
  // Only the servers given here: the user's are disabled for this thread,
  // HodeIA's tools are approved one by one (approvalPolicy "never" refuses
  // any other).
  QJsonObject servers;
  for (const QString &name : userMcpServers)
    servers.insert(name, QJsonObject{{"enabled", false}});
  for (const McpServer &s : options.mcpServers) {
    QJsonObject env;
    for (auto it = s.env.cbegin(); it != s.env.cend(); ++it)
      env.insert(it.key(), it.value());
    QJsonObject tools;
    for (const QString &t : options.allowedTools)
      if (t.section('/', 0, 0) == s.name)
        tools.insert(t.section('/', 1), QJsonObject{{"approval_mode", "approve"}});
    QJsonObject server{{"command", s.command},
                       {"args", QJsonArray::fromStringList(s.args)},
                       {"enabled", true}};
    if (!env.isEmpty())
      server["env"] = env;
    if (!tools.isEmpty())
      server["tools"] = tools;
    servers.insert(s.name, server);
  }
  if (!servers.isEmpty())
    config["mcp_servers"] = servers;

  QJsonObject params{{"cwd", QDir::toNativeSeparators(QDir(options.workDir).absolutePath())},
                     {"approvalPolicy", "never"},
                     {"sandbox", "read-only"}};
  if (!options.systemPrompt.isEmpty())
    params["baseInstructions"] = options.systemPrompt;
  if (!options.model.isEmpty() && options.model != "default")
    params["model"] = options.model;
  if (!config.isEmpty())
    params["config"] = config;
  return params;
}

QJsonArray CodexConnection::inputItems(const Prompt &prompt) {
  QJsonArray items;
  for (const PromptPart &part : prompt)
    items.append(part.isImage()
                     ? QJsonObject{{"type", "image"},
                                   {"url", "data:" + part.mimeType + ";base64," +
                                               QString::fromLatin1(part.base64)}}
                     : QJsonObject{{"type", "text"}, {"text", part.text}});
  return items;
}

void CodexConnection::call(const QString &method, const QJsonObject &params, Callback done) {
  const qint64 id = m_nextId++;
  m_pending.insert(id, std::move(done));
  m_process.write({{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}});
}

void CodexConnection::fail(const QString &error) {
  if (m_failed)
    return;
  m_failed = true;
  m_killTimer.stop();
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

void CodexConnection::start(std::function<void()> ready) {
  m_failed = false;
  const QString dir = m_options.workDir.isEmpty() ? QDir::tempPath() : m_options.workDir;
  if (!m_process.start(m_profile.command, {"app-server"}, dir, m_profile.env)) {
    fail(startFailureMessage(m_profile, m_process.lastError()));
    return;
  }
  call("initialize",
       {{"clientInfo", QJsonObject{{"name", "hodeia"},
                                   {"title", "HodeIA"},
                                   {"version", QCoreApplication::applicationVersion()}}}},
       [this, ready = std::move(ready)](const QJsonValue &, const QJsonObject *e) {
         if (e) {
           fail(tr("Codex did not start: %1").arg(errorText(*e)));
           return;
         }
         m_process.write({{"jsonrpc", "2.0"}, {"method", "initialized"}});
         ready();
       });
}

void CodexConnection::open(const SessionOptions &options) {
  m_options = options;
  start([this]() {
    // The user's MCP servers, to disable them for this thread.
    call("config/read", {{"cwd", QDir(m_options.workDir).absolutePath()}},
         [this](const QJsonValue &result, const QJsonObject *e) {
           QStringList user;
           if (!e)
             user = result.toObject().value("config").toObject()
                        .value("mcp_servers").toObject().keys();
           for (const McpServer &s : m_options.mcpServers)
             user.removeAll(s.name);
           startThread(user, !m_options.resumeSessionId.isEmpty());
         });
  });
}

void CodexConnection::startThread(const QStringList &userMcpServers, bool resume) {
  QJsonObject params = threadParams(m_options, userMcpServers);
  if (resume)
    params["threadId"] = m_options.resumeSessionId;
  call(resume ? "thread/resume" : "thread/start", params,
       [this, userMcpServers, resume](const QJsonValue &result, const QJsonObject *e) {
         if (e) {
           if (resume) {
             qInfo() << "[Codex] thread not resumed:" << errorText(*e);
             startThread(userMcpServers, false);
           } else {
             fail(errorText(*e));
           }
           return;
         }
         m_threadId = result.toObject().value("thread").toObject().value("id").toString();
         m_resumed = resume;
         emit opened();
       });
}

void CodexConnection::requestModels() {
  start([this]() { fetchModels({}, {}); });
}

void CodexConnection::fetchModels(const QString &cursor, QStringList models) {
  QJsonObject params;
  if (!cursor.isEmpty())
    params["cursor"] = cursor;
  call("model/list", params,
       [this, models](const QJsonValue &result, const QJsonObject *e) mutable {
         if (e) {
           fail(errorText(*e));
           return;
         }
         const QJsonObject r = result.toObject();
         for (const QJsonValue &v : r.value("data").toArray()) {
           const QJsonObject m = v.toObject();
           if (!m.value("hidden").toBool())
             models.append(m.value("model").toString(m.value("id").toString()));
         }
         const QString next = r.value("nextCursor").toString();
         if (next.isEmpty())
           emit modelsListed(models);
         else
           fetchModels(next, models);
       });
}

void CodexConnection::prompt(const Prompt &prompt) {
  if (m_failed || m_threadId.isEmpty()) {
    emit turnFinished({TurnResult::Stop::Error, {}, tr("No open session.")});
    return;
  }
  m_answer.clear();
  m_lastItem.clear();
  m_turnError.clear();
  m_turnId.clear();
  m_cancelRequested = false;
  m_inTurn = true;
  call("turn/start", {{"threadId", m_threadId}, {"input", inputItems(prompt)}},
       [this](const QJsonValue &result, const QJsonObject *e) {
         if (e) {
           finishTurn({TurnResult::Stop::Error, m_answer, errorText(*e)});
           return;
         }
         if (m_turnId.isEmpty())
           m_turnId = result.toObject().value("turn").toObject().value("id").toString();
         if (m_cancelRequested && m_inTurn)
           call("turn/interrupt", {{"threadId", m_threadId}, {"turnId", m_turnId}},
                [](const QJsonValue &, const QJsonObject *) {});
       });
}

void CodexConnection::cancel() {
  if (!m_inTurn) {
    if (m_threadId.isEmpty())
      fail(tr("Cancelled."));
    return;
  }
  m_cancelRequested = true;
  if (!m_turnId.isEmpty()) // else once turn/start answers
    call("turn/interrupt", {{"threadId", m_threadId}, {"turnId", m_turnId}},
         [](const QJsonValue &, const QJsonObject *) {});
  m_killTimer.start(kInterruptGraceMs);
}

void CodexConnection::close() {
  m_failed = true; // no signal from here on
  m_killTimer.stop();
  m_pending.clear();
  m_process.stop();
}

void CodexConnection::finishTurn(const TurnResult &result) {
  if (!m_inTurn)
    return;
  m_inTurn = false;
  m_killTimer.stop();
  emit turnFinished(result);
}

void CodexConnection::addAnswer(const QString &itemId, const QString &text, bool delta) {
  // The messages of one turn (before and after tool calls) are separated by
  // a blank line, as for the other agents.
  QString piece = text;
  if (itemId != m_lastItem) {
    if (!m_answer.isEmpty())
      piece.prepend(QStringLiteral("\n\n"));
    m_lastItem = itemId;
  }
  m_answer += piece;
  if (delta == m_options.stream)
    emit agentEvent({Event::Kind::Text, piece, {}, {}});
}

void CodexConnection::handle(const QJsonObject &msg) {
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
  } else if (msg.contains("id")) {
    answerServerRequest(msg);
  } else {
    handleNotification(method, msg.value("params").toObject());
  }
}

void CodexConnection::handleNotification(const QString &method, const QJsonObject &params) {
  if (params.contains("threadId") && params.value("threadId").toString() != m_threadId)
    return; // another thread (a sub-agent...)
  if (!m_inTurn)
    return;
  if (method == "turn/started") {
    if (m_turnId.isEmpty())
      m_turnId = params.value("turn").toObject().value("id").toString();
  } else if (method == "item/agentMessage/delta") {
    if (m_options.stream)
      addAnswer(params.value("itemId").toString(), params.value("delta").toString(), true);
  } else if (method == "item/reasoning/summaryTextDelta" || method == "item/reasoning/textDelta") {
    if (m_options.stream)
      emit agentEvent({Event::Kind::Thought, params.value("delta").toString(), {}, {}});
  } else if (method == "item/started" || method == "item/completed") {
    const QJsonObject item = params.value("item").toObject();
    const QString type = item.value("type").toString();
    const bool done = method == "item/completed";
    if (type == "agentMessage") {
      if (done && !m_options.stream)
        addAnswer(item.value("id").toString(), item.value("text").toString(), false);
      else if (done && m_lastItem != item.value("id").toString()) // no delta came
        addAnswer(item.value("id").toString(), item.value("text").toString(), true);
    } else if (type == "reasoning") {
      if (done && !m_options.stream) {
        QStringList parts;
        for (const QJsonValue &s : item.value("summary").toArray())
          parts.append(s.toString());
        if (!parts.isEmpty())
          emit agentEvent({Event::Kind::Thought, parts.join("\n\n"), {}, {}});
      }
    } else if (type == "mcpToolCall" || type == "dynamicToolCall" ||
               type == "commandExecution" || type == "fileChange" || type == "webSearch") {
      QString title = type == "mcpToolCall"
                          ? item.value("server").toString() + "/" + item.value("tool").toString()
                      : type == "commandExecution" ? item.value("command").toString()
                      : type == "webSearch"        ? "Web: " + item.value("query").toString()
                                                   : item.value("tool").toString(type);
      QString status = QStringLiteral("in_progress");
      if (done) // a web search has no status: done is done
        status = !item.contains("status") || item.value("status").toString() == "completed"
                     ? QStringLiteral("completed")
                     : QStringLiteral("failed");
      emit agentEvent({Event::Kind::ToolCall, title, item.value("id").toString(), status});
    }
  } else if (method == "error") {
    const QString text = turnErrorText(params.value("error").toObject());
    if (params.value("willRetry").toBool())
      emit agentEvent({Event::Kind::Notice, text, {}, {}});
    else
      m_turnError = text;
  } else if (method == "turn/completed") {
    const QJsonObject turn = params.value("turn").toObject();
    const QString status = turn.value("status").toString();
    if (m_answer.isEmpty()) // no item notification: the turn's own items
      for (const QJsonValue &v : turn.value("items").toArray())
        if (v.toObject().value("type").toString() == "agentMessage")
          addAnswer(v.toObject().value("id").toString(), v.toObject().value("text").toString(),
                    m_options.stream);
    if (status == "interrupted" || m_cancelRequested) {
      finishTurn({TurnResult::Stop::Cancelled, m_answer, {}});
    } else if (status == "failed") {
      QString error = turnErrorText(turn.value("error").toObject());
      if (error.isEmpty())
        error = m_turnError.isEmpty() ? tr("The turn failed.") : m_turnError;
      finishTurn({TurnResult::Stop::Error, m_answer, error});
    } else {
      finishTurn({TurnResult::Stop::EndTurn, m_answer, {}});
    }
  }
}

void CodexConnection::answerServerRequest(const QJsonObject &msg) {
  // approvalPolicy "never": Codex should not ask.  If it does, the answer
  // is no (HodeIA's tools are approved in the thread's config).
  const QString method = msg.value("method").toString();
  QJsonObject result;
  if (method == "item/commandExecution/requestApproval" ||
      method == "item/fileChange/requestApproval")
    result = {{"decision", "decline"}};
  else if (method == "applyPatchApproval" || method == "execCommandApproval")
    result = {{"decision", "denied"}};
  else if (method == "mcpServer/elicitation/request")
    result = {{"action", "decline"}};
  qInfo() << "[Codex] request refused:" << method;
  if (result.isEmpty())
    m_process.write({{"jsonrpc", "2.0"}, {"id", msg.value("id")},
                     {"error", QJsonObject{{"code", kMethodNotFound},
                                           {"message", "Not supported by HodeIA: " + method}}}});
  else
    m_process.write({{"jsonrpc", "2.0"}, {"id", msg.value("id")}, {"result", result}});
}

Connection *CodexConnector::create(const Profile &profile, QObject *parent) const {
  return new CodexConnection(profile, parent);
}

std::expected<QStringList, QString> CodexConnector::listModels(const Profile &profile) const {
  if (!profile.models.isEmpty())
    return profile.models;
  CodexConnection connection(profile);
  QEventLoop loop;
  bool done = false;
  std::expected<QStringList, QString> out = std::unexpected(tr("Codex did not answer."));
  QObject::connect(&connection, &CodexConnection::modelsListed, &loop,
                   [&](const QStringList &models) {
                     out = QStringList{"default"} + models;
                     done = true;
                     loop.quit();
                   });
  QObject::connect(&connection, &Connection::failed, &loop, [&](const QString &error) {
    out = std::unexpected(error);
    done = true;
    loop.quit();
  });
  QTimer::singleShot(kOpenTimeoutMs, &loop, &QEventLoop::quit);
  connection.requestModels();
  if (!done) // it may fail at once (not installed)
    loop.exec();
  connection.close();
  return out;
}

} // namespace Agents

std::unique_ptr<Agents::Connector> hodeia_connector_codex() {
  return std::make_unique<Agents::CodexConnector>();
}
