#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <expected>
#include <optional>

class SqliteDb;

/// A conversation with an agent (mode agentique of the Query window).
struct ConversationRow {
  int id = 0;
  QString title;
  QString backend;      ///< name of the agent backend (Backends window)
  QString model;
  QString agentSession; ///< the agent's session id, to resume it; may be empty
  QDateTime createdAt;
  QDateTime updatedAt;
  int messageCount = 0;
};

/// One message of a conversation.  The assistant's text is stored as the
/// agent wrote it (citation marks [@id:pN] included): it is rendered when
/// shown, so citations follow the current metadata.
struct ConversationMessage {
  enum class Role { User, Assistant, Error };
  int id = 0;
  Role role = Role::User;
  QString content;
  QDateTime createdAt;
};

QString conversationRoleName(ConversationMessage::Role role);

/// SQL access to the conversation tables of an open project database.
/// No Qt Quick: usable from any thread holding the (FULLMUTEX) connection.
class ConversationRepository {
public:
  explicit ConversationRepository(SqliteDb &db);

  /// Every conversation, the most recently active first.
  QList<ConversationRow> conversations() const;
  std::optional<ConversationRow> conversation(int id) const;

  /// A new conversation; its id.
  std::expected<int, QString> create(const QString &title, const QString &backend,
                                     const QString &model);
  std::expected<void, QString> rename(int id, const QString &title);
  std::expected<void, QString> setAgent(int id, const QString &backend,
                                        const QString &model, const QString &agentSession);
  /// Delete conversations and their messages.
  std::expected<void, QString> remove(const QList<int> &ids);

  /// The messages of a conversation, in order.
  QList<ConversationMessage> messages(int conversationId) const;
  /// Append a message and mark the conversation active now; its id.
  std::expected<int, QString> addMessage(int conversationId, ConversationMessage::Role role,
                                         const QString &content);

private:
  SqliteDb &m_db;
};
