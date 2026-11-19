#pragma once

#include "agentconnection.h"
#include "conversationrepository.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantList>

class ConfigManager;
class DatabaseManager;
struct PaperLabel;

/// The mode agentique of the Query window (exposed to QML as agentChat):
/// conversations with a coding agent (Claude Code, Qwen Code...) that
/// searches the library itself through HodeIA's MCP tools and suggests ways
/// to take the research further.
///
/// One live agent connection, for the open conversation; it is resumed with
/// the agent's session id when a stored conversation is opened again (or,
/// failing that, the agent gets the conversation so far).  Messages are
/// stored as the agent wrote them; QML gets them with the citation marks
/// replaced by author-year citations and a Sources section.  Runs on the GUI
/// thread; the agent works in its own process.
class AgentChatController : public QObject {
  Q_OBJECT
  /// [{id, title, backend, model, updatedAt, messageCount}], newest first.
  Q_PROPERTY(QVariantList conversations READ conversations NOTIFY conversationsChanged)
  /// The open conversation; 0 = a new one, created by the first message.
  Q_PROPERTY(int currentId READ currentId NOTIFY currentChanged)
  /// [{role: "user"|"assistant"|"error", text}] of the open conversation;
  /// assistant text in Markdown, citations resolved.
  Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
  /// Agent backends that can search the library ({name, agent, model}).
  Q_PROPERTY(QVariantList agentBackends READ agentBackends NOTIFY agentBackendsChanged)
  /// Backend and model of the open conversation (or of the next new one).
  Q_PROPERTY(QString backend READ backend NOTIFY currentChanged)
  Q_PROPERTY(QString model READ model NOTIFY currentChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  /// What the agent is doing ("Searching the library..."), while busy.
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
  AgentChatController(ConfigManager *config, DatabaseManager *database,
                      QObject *parent = nullptr);
  ~AgentChatController() override;

  QVariantList conversations() const;
  int currentId() const { return m_currentId; }
  QVariantList messages() const;
  QVariantList agentBackends() const;
  QString backend() const { return m_backend; }
  QString model() const { return m_model; }
  bool busy() const { return m_busy; }
  QString status() const { return m_status; }

  /// Start a new conversation with this agent backend and model.
  Q_INVOKABLE void newConversation(const QString &backend, const QString &model);
  Q_INVOKABLE void openConversation(int id);
  /// Send a message in the open conversation.
  Q_INVOKABLE void send(const QString &text);
  /// Stop the agent's answer.
  Q_INVOKABLE void stop();
  Q_INVOKABLE QString rename(int id, const QString &title);
  /// Delete conversations; an empty string, or the error.
  Q_INVOKABLE QString remove(const QVariantList &ids);
  /// Stop the agent (window closed).
  Q_INVOKABLE void closeAgent();

  /// The instructions given to the agent in mode agentique.
  static QString systemPrompt();
  /// HodeIA's MCP server, as given to the agent.
  static Agents::McpServer hodeiaMcpServer();
  /// The conversation so far, for an agent that could not resume it.
  static QString transcript(const QList<ConversationMessage> &messages);

signals:
  void conversationsChanged();
  void currentChanged();
  void messagesChanged();
  void agentBackendsChanged();
  void busyChanged();
  void statusChanged();
  /// Something went wrong outside a message (database...).
  void errorOccurred(const QString &message);

private:
  void setBusy(bool busy);
  void setStatus(const QString &status);
  void dropConnection();
  void startTurn(const QString &text);
  void onOpened();
  void onEvent(const Agents::Event &event);
  void onTurnFinished(const Agents::TurnResult &result);
  void onFailed(const QString &error);
  /// The stored agent session is gone: forget it, start a new session and
  /// send `text` again (with the conversation so far).
  void retryWithoutResume(const QString &text, const QString &why);
  void addMessage(ConversationMessage::Role role, const QString &content);
  QHash<QString, PaperLabel> paperLabels() const;

  ConfigManager *m_config;
  DatabaseManager *m_database;
  int m_currentId = 0;
  QString m_backend;
  QString m_model;
  bool m_busy = false;
  QString m_status;
  QPointer<Agents::Connection> m_connection;
  int m_connectionFor = 0;      ///< conversation the connection belongs to
  QString m_pendingText;        ///< message waiting for the connection
  bool m_needsTranscript = false;
  bool m_resuming = false;      ///< the connection opens a stored session
  QString m_firstTurn;          ///< first message after a resume, to retry it
  bool m_stopped = false;
};
