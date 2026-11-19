#include "piconnector.h"

#include "agentsync.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <memory>

namespace Agents {

namespace {


} // namespace

PiConnection::PiConnection(const Profile &profile, QObject *parent)
    : Connection(parent), m_profile(profile) {
  connect(&m_process, &AgentProcess::message, this, &PiConnection::handle);
  connect(&m_process, &AgentProcess::exited, this, [this](const QString &reason) {
    QString detail = reason;
    if (!m_process.stderrTail().isEmpty())
      detail += "\n" + m_process.stderrTail().right(800);
    fail(detail);
  });
}

PiConnection::~PiConnection() { close(); }

QStringList PiConnection::arguments(const SessionOptions &options) {
  QStringList args{"--mode", "rpc", "--no-session"};
  if (!options.model.isEmpty() && options.model != "default")
    args << "--model" << options.model;
  if (!options.systemPrompt.isEmpty())
    args << "--system-prompt" << options.systemPrompt;
  if (!options.tools)
    args << "--no-tools" << "--no-extensions" << "--no-skills"
         << "--no-prompt-templates" << "--no-context-files";
  // Pi's own MCP support would read the user's servers: HodeIA's are given
  // through ACP/Claude only for now.
  args << "--no-mcp";
  return args;
}

void PiConnection::command(const QJsonObject &cmd, Callback done) {
  const QString id = QString("hodeia-%1").arg(m_nextId++);
  if (done)
    m_pending.insert(id, std::move(done));
  QJsonObject withId = cmd;
  withId["id"] = id;
  m_process.write(withId);
}

void PiConnection::fail(const QString &error) {
  if (m_closed)
    return;
  m_closed = true;
  m_pending.clear();
  finishTurn({TurnResult::Stop::Error, m_answer, error});
  emit failed(error);
}

void PiConnection::open(const SessionOptions &options) {
  m_closed = false;
  if (!m_process.start(m_profile.command, arguments(options), options.workDir,
                       m_profile.env)) {
    const QString error = startFailureMessage(m_profile, m_process.lastError());
    fail(error);
    return;
  }
  command({{"type", "get_available_models"}}, [this](const QJsonObject &r) {
    if (!r.value("success").toBool()) {
      fail(r.value("error").toString());
      return;
    }
    for (const QJsonValue &m : r.value("data").toObject().value("models").toArray()) {
      const QJsonObject model = m.toObject();
      m_models.append(model.value("provider").toString() + "/" +
                      model.value("id").toString());
    }
    command({{"type", "get_state"}}, [this](const QJsonObject &state) {
      m_sessionId = state.value("data").toObject().value("sessionId").toString();
      emit opened();
    });
  });
}

void PiConnection::prompt(const Prompt &prompt) {
  if (m_closed || !m_process.isRunning()) {
    emit turnFinished({TurnResult::Stop::Error, {}, QCoreApplication::translate("Agents::PiConnection", "No open session.")});
    return;
  }
  QJsonArray images;
  for (const PromptPart &part : prompt)
    if (part.isImage())
      images.append(QJsonObject{{"type", "image"},
                                {"data", QString::fromLatin1(part.base64)},
                                {"mimeType", part.mimeType}});
  QJsonObject cmd{{"type", "prompt"}, {"message", promptText(prompt)}};
  if (!images.isEmpty())
    cmd["images"] = images;
  m_answer.clear();
  m_error.clear();
  m_inTurn = true;
  command(cmd, [this](const QJsonObject &r) {
    if (!r.value("success").toBool())
      finishTurn({TurnResult::Stop::Error, {}, r.value("error").toString()});
    else if (r.value("data").toObject().value("disposition").toString() == "handled")
      finishTurn({TurnResult::Stop::EndTurn, m_answer, {}});
  });
}

void PiConnection::cancel() {
  if (m_inTurn)
    command({{"type", "abort"}});
}

void PiConnection::close() {
  m_closed = true;
  m_pending.clear();
  m_process.stop();
}

void PiConnection::finishTurn(const TurnResult &result) {
  if (!m_inTurn)
    return;
  m_inTurn = false;
  emit turnFinished(result);
}

void PiConnection::handle(const QJsonObject &msg) {
  const QString type = msg.value("type").toString();
  if (type == "response") {
    Callback done = m_pending.take(msg.value("id").toString());
    if (done)
      done(msg);
    return;
  }
  if (type == "message_end") {
    const QJsonObject message = msg.value("message").toObject();
    if (message.value("role").toString() != "assistant")
      return;
    QString text;
    for (const QJsonValue &b : message.value("content").toArray())
      if (b.toObject().value("type").toString() == "text")
        text += b.toObject().value("text").toString();
    m_answer += text;
    if (!text.isEmpty())
      emit event({Event::Kind::Text, text, {}, {}});
    if (message.value("stopReason").toString() == "error")
      m_error = message.value("errorMessage").toString();
  } else if (type == "tool_execution_start") {
    emit event({Event::Kind::ToolCall, msg.value("toolName").toString(),
                msg.value("toolCallId").toString(), QStringLiteral("in_progress")});
  } else if (type == "auto_retry_start") {
    emit event({Event::Kind::Notice, msg.value("errorMessage").toString(), {}, {}});
  } else if (type == "agent_settled") {
    if (msg.value("aborted").toBool())
      finishTurn({TurnResult::Stop::Cancelled, m_answer, {}});
    else if (!m_error.isEmpty())
      finishTurn({TurnResult::Stop::Error, m_answer, m_error});
    else
      finishTurn({TurnResult::Stop::EndTurn, m_answer, {}});
  }
}

Connection *PiConnector::create(const Profile &profile, QObject *parent) const {
  return new PiConnection(profile, parent);
}

std::expected<QStringList, QString> PiConnector::listModels(const Profile &profile) const {
  if (!profile.models.isEmpty())
    return profile.models;
  PiConnection connection(profile);
  SessionOptions options;
  options.workDir = QDir::tempPath();
  if (auto r = openSync(connection, options, kOpenTimeoutMs); !r)
    return std::unexpected(r.error());
  QStringList models = connection.models();
  connection.close();
  return models;
}

} // namespace Agents

std::unique_ptr<Agents::Connector> hodeia_connector_pi() {
  return std::make_unique<Agents::PiConnector>();
}
