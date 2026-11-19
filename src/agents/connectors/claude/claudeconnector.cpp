#include "claudeconnector.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <memory>

namespace Agents {

namespace {

/// After an interrupt, the time left to the turn to end before the kill.
constexpr int kInterruptGraceMs = 5000;


} // namespace

ClaudeConnection::ClaudeConnection(const Profile &profile, QObject *parent)
    : Connection(parent), m_profile(profile) {
  m_killTimer.setSingleShot(true);
  connect(&m_killTimer, &QTimer::timeout, this, [this]() {
    qWarning() << "[Claude] the turn did not stop: killing the process";
    m_process.kill();
    finishTurn({TurnResult::Stop::Cancelled, m_answer, {}});
    m_closed = true;
    emit failed(QCoreApplication::translate("Agents::ClaudeConnection", "The agent was stopped."));
  });
  connect(&m_process, &AgentProcess::message, this, &ClaudeConnection::handle);
  connect(&m_process, &AgentProcess::exited, this, [this](const QString &reason) {
    if (m_closed)
      return;
    QString detail = reason;
    if (!m_process.stderrTail().isEmpty())
      detail += "\n" + m_process.stderrTail().right(800);
    finishTurn({TurnResult::Stop::Error, m_answer, detail});
    m_closed = true;
    emit failed(detail);
  });
}

ClaudeConnection::~ClaudeConnection() { close(); }

QStringList ClaudeConnection::arguments(const SessionOptions &options) {
  QStringList args{"-p", "--input-format", "stream-json", "--output-format",
                   "stream-json", "--verbose"};
  if (!options.model.isEmpty() && options.model != "default")
    args << "--model" << options.model;
  if (!options.systemPrompt.isEmpty())
    args << "--system-prompt" << options.systemPrompt;
  if (!options.tools)
    args << "--tools" << "" << "--setting-sources" << "";
  // Only the MCP servers given here, never the user's.
  args << "--strict-mcp-config";
  if (!options.mcpServers.isEmpty()) {
    QJsonObject servers;
    for (const McpServer &s : options.mcpServers) {
      QJsonObject env;
      for (auto it = s.env.cbegin(); it != s.env.cend(); ++it)
        env.insert(it.key(), it.value());
      servers.insert(s.name, QJsonObject{{"type", "stdio"},
                                         {"command", s.command},
                                         {"args", QJsonArray::fromStringList(s.args)},
                                         {"env", env}});
    }
    args << "--mcp-config"
         << QString::fromUtf8(QJsonDocument(QJsonObject{{"mcpServers", servers}})
                                  .toJson(QJsonDocument::Compact));
  }
  return args;
}

QJsonObject ClaudeConnection::userMessage(const Prompt &prompt) {
  QJsonArray content;
  for (const PromptPart &part : prompt)
    content.append(part.isImage()
                       ? QJsonObject{{"type", "image"},
                                     {"source", QJsonObject{{"type", "base64"},
                                                            {"media_type", part.mimeType},
                                                            {"data", QString::fromLatin1(part.base64)}}}}
                       : QJsonObject{{"type", "text"}, {"text", part.text}});
  return {{"type", "user"},
          {"message", QJsonObject{{"role", "user"}, {"content", content}}}};
}

void ClaudeConnection::open(const SessionOptions &options) {
  m_closed = false;
  if (!m_process.start(m_profile.command, arguments(options), options.workDir,
                       m_profile.env)) {
    const QString error = startFailureMessage(m_profile, m_process.lastError());
    m_closed = true;
    emit failed(error);
    return;
  }
  // Claude Code says nothing before the first message: started is enough.
  emit opened();
}

void ClaudeConnection::prompt(const Prompt &prompt) {
  if (m_closed || !m_process.isRunning()) {
    emit turnFinished({TurnResult::Stop::Error, {}, QCoreApplication::translate("Agents::ClaudeConnection", "No open session.")});
    return;
  }
  m_answer.clear();
  m_notice.clear();
  m_cancelled = false;
  m_inTurn = true;
  m_process.write(userMessage(prompt));
}

void ClaudeConnection::cancel() {
  if (!m_inTurn)
    return;
  m_cancelled = true;
  m_process.write({{"type", "control_request"},
                   {"request_id", QString("hodeia-%1").arg(++m_controlId)},
                   {"request", QJsonObject{{"subtype", "interrupt"}}}});
  m_killTimer.start(kInterruptGraceMs);
}

void ClaudeConnection::close() {
  m_closed = true;
  m_killTimer.stop();
  m_process.stop();
}

void ClaudeConnection::finishTurn(const TurnResult &result) {
  if (!m_inTurn)
    return;
  m_inTurn = false;
  m_killTimer.stop();
  emit turnFinished(result);
}

void ClaudeConnection::handle(const QJsonObject &msg) {
  const QString type = msg.value("type").toString();
  if (type == "system") {
    const QString id = msg.value("session_id").toString();
    if (!id.isEmpty())
      m_sessionId = id;
  } else if (type == "assistant") {
    for (const QJsonValue &v : msg.value("message").toObject().value("content").toArray()) {
      const QJsonObject block = v.toObject();
      const QString kind = block.value("type").toString();
      if (kind == "text") {
        const QString text = block.value("text").toString();
        m_answer += text;
        emit event({Event::Kind::Text, text, {}, {}});
      } else if (kind == "thinking") {
        emit event({Event::Kind::Thought, block.value("thinking").toString(), {}, {}});
      } else if (kind == "tool_use") {
        emit event({Event::Kind::ToolCall, block.value("name").toString(),
                    block.value("id").toString(), QStringLiteral("in_progress")});
      }
    }
  } else if (type == "rate_limit_event") {
    const QJsonObject info = msg.value("rate_limit_info").toObject();
    if (info.value("status").toString() == "rejected") {
      m_notice = QCoreApplication::translate("Agents::ClaudeConnection", "Claude usage limit reached.");
      emit event({Event::Kind::Notice, m_notice, {}, {}});
    }
  } else if (type == "result") {
    const bool isError = msg.value("is_error").toBool();
    const QString result = msg.value("result").toString();
    if (m_cancelled) {
      finishTurn({TurnResult::Stop::Cancelled, m_answer, {}});
    } else if (isError) {
      // The error text (a usage limit...) comes as the result, sometimes
      // also as assistant text: not an answer.
      finishTurn({TurnResult::Stop::Error, {},
                  result.isEmpty() ? msg.value("subtype").toString() : result});
    } else {
      const QString stop = msg.value("stop_reason").toString();
      finishTurn({stop == "max_tokens" ? TurnResult::Stop::MaxTokens
                                       : TurnResult::Stop::EndTurn,
                  m_answer.isEmpty() ? result : m_answer, {}});
    }
  } else if (type == "control_response") {
    qDebug() << "[Claude] control response:"
             << msg.value("response").toObject().value("subtype").toString();
  }
}

Connection *ClaudeConnector::create(const Profile &profile, QObject *parent) const {
  return new ClaudeConnection(profile, parent);
}

std::expected<QStringList, QString> ClaudeConnector::listModels(const Profile &profile) const {
  if (!profile.models.isEmpty())
    return profile.models;
  return QStringList{"default", "opus", "sonnet", "haiku"};
}

} // namespace Agents

std::unique_ptr<Agents::Connector> hodeia_connector_claude() {
  return std::make_unique<Agents::ClaudeConnector>();
}
