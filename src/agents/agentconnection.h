#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtPlugin>
#include <expected>

/* The common ground of the agent connectors.  HodeIA talks to coding agents
   (Claude Code, Qwen Code, Pi...) through one interface, Agents::Connection;
   each connector (src/agents/connectors/<name>/) implements it for one way of
   talking to agents: ACP, or an agent's own command-line protocol.

   Nothing here depends on the rest of HodeIA, so that a connector can later
   be built as a Qt plugin (.so/.dll) without change: Connector is declared
   as a plugin interface. */

namespace Agents {

/// A piece of a prompt, in order: text, or an image.
struct PromptPart {
  QString text;       ///< text part (empty for an image)
  QString mimeType;   ///< image part: "image/jpeg"...
  QByteArray base64;  ///< image part: the data, base64-encoded

  bool isImage() const { return !base64.isEmpty(); }
  static PromptPart textPart(const QString &text) { return {text, {}, {}}; }
  static PromptPart image(const QString &mime, const QByteArray &data) {
    return {{}, mime, data};
  }
};
using Prompt = QList<PromptPart>;

/// The text parts of a prompt, joined.
QString promptText(const Prompt &prompt);

/// A stdio MCP server given to the agent (mode agentique).
struct McpServer {
  QString name;
  QString command;
  QStringList args;
  QMap<QString, QString> env;
};

struct SessionOptions {
  QString workDir;      ///< the agent's working directory (absolute)
  QString systemPrompt; ///< replaces the agent's own when it allows it
  QString model;        ///< one of listModels(); empty or "default" = the agent's
  /// false (classic mode): no built-in tool and none of the user's settings
  /// (skills, CLAUDE.md...) when the agent lets us say so.
  bool tools = false;
  QList<McpServer> mcpServers;
};

/// Something the agent did during a turn.
struct Event {
  enum class Kind {
    Text,     ///< answer text (`text`: the new piece)
    Thought,  ///< reasoning shown by the agent
    ToolCall, ///< a tool step (`text`: its title, `status`: pending...)
    Notice,   ///< information from the agent (rate limit...)
  };
  Kind kind = Kind::Text;
  QString text;
  QString id;     ///< tool call id
  QString status; ///< tool call: pending, in_progress, completed, failed
};

struct TurnResult {
  enum class Stop { EndTurn, Cancelled, Refusal, MaxTokens, Error };
  Stop stop = Stop::EndTurn;
  QString text;  ///< the whole answer of the turn (also when cancelled)
  QString error; ///< Stop::Error: what went wrong, for the user
};

/// One session with one agent process.  Asynchronous: results come as
/// signals, from the event loop of the thread that owns the connection.
class Connection : public QObject {
  Q_OBJECT

public:
  using QObject::QObject;

  /// Start the agent and open a session: opened(), or failed().
  virtual void open(const SessionOptions &options) = 0;
  /// One turn, after opened() and one at a time: event()s, then
  /// turnFinished().  The agent keeps the conversation between turns.
  virtual void prompt(const Prompt &prompt) = 0;
  /// Stop the running turn: turnFinished() follows with Stop::Cancelled.
  virtual void cancel() = 0;
  /// Stop the agent (waits up to a few seconds).  No signal afterwards.
  virtual void close() = 0;
  /// The agent's session id once known (to resume it later); may be empty.
  virtual QString sessionId() const = 0;

signals:
  void opened();
  /// The session could not be opened, or the agent stopped: no more turns.
  void failed(const QString &error);
  void event(const Agents::Event &event);
  void turnFinished(const Agents::TurnResult &result);
};

/// How to start one agent: a built-in or user profile (profiles/*.json).
struct Profile {
  QString id;          ///< "claude", "qwen"...
  QString name;        ///< for the user
  QString connector;   ///< Connector::id() that talks to it
  QString command;     ///< command line that starts it (args included)
  QMap<QString, QString> env;
  bool verified = false;  ///< tried with HodeIA
  QString installHint;    ///< how to install it, for error messages
  /// Runtimes it needs besides its own program: "node" (Node.js), "uv".
  QStringList runtimes;
  QString loginHint;      ///< how to log in, for error messages
  QStringList models;     ///< fixed model list, when the agent cannot list them
  QJsonObject options;    ///< connector-specific settings
};

/// A way of talking to agents.  One per folder of src/agents/connectors.
class Connector {
public:
  virtual ~Connector() = default;
  virtual QString id() const = 0;
  virtual Connection *create(const Profile &profile, QObject *parent = nullptr) const = 0;
  /// The models of the agent, for the Backends window.  Blocking (may start
  /// the agent for a few seconds): call it from a worker thread.
  virtual std::expected<QStringList, QString> listModels(const Profile &profile) const = 0;
};

} // namespace Agents

Q_DECLARE_METATYPE(Agents::Event)
Q_DECLARE_METATYPE(Agents::TurnResult)
#define HodeiaAgentConnector_iid "org.hodeia.Agents.Connector/1"
Q_DECLARE_INTERFACE(Agents::Connector, HodeiaAgentConnector_iid)
