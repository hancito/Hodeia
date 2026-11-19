#include "conversationrepository.h"

#include "sqlitedb.h"

#include <QTimeZone>
#include <Sqlite3/sqlite3.h>

namespace {

using Error = std::unexpected<QString>;

QString lastError(SqliteDb &db) {
  return QString::fromUtf8(sqlite3_errmsg(db.handle()));
}

ConversationMessage::Role roleFromName(const QString &name) {
  if (name == "assistant")
    return ConversationMessage::Role::Assistant;
  if (name == "error")
    return ConversationMessage::Role::Error;
  return ConversationMessage::Role::User;
}

constexpr const char *kSelectConversation =
    "SELECT c.id, c.title, c.backend, c.model, c.agent_session, c.created_at, "
    "c.updated_at, (SELECT COUNT(*) FROM ConversationMessages m "
    "WHERE m.conversation_id = c.id) FROM Conversations c ";

ConversationRow readConversation(const SqliteStatement &s) {
  ConversationRow r;
  r.id = s.columnInt(0);
  r.title = s.columnText(1);
  r.backend = s.columnText(2);
  r.model = s.columnText(3);
  r.agentSession = s.columnText(4);
  r.createdAt = QDateTime::fromString(s.columnText(5), "yyyy-MM-dd HH:mm:ss");
  r.createdAt.setTimeZone(QTimeZone::UTC);
  r.updatedAt = QDateTime::fromString(s.columnText(6), "yyyy-MM-dd HH:mm:ss");
  r.updatedAt.setTimeZone(QTimeZone::UTC);
  r.messageCount = s.columnInt(7);
  return r;
}

} // namespace

QString conversationRoleName(ConversationMessage::Role role) {
  switch (role) {
  case ConversationMessage::Role::Assistant:
    return QStringLiteral("assistant");
  case ConversationMessage::Role::Error:
    return QStringLiteral("error");
  case ConversationMessage::Role::User:
    break;
  }
  return QStringLiteral("user");
}

ConversationRepository::ConversationRepository(SqliteDb &db) : m_db(db) {}

QList<ConversationRow> ConversationRepository::conversations() const {
  QList<ConversationRow> list;
  SqliteStatement s =
      m_db.prepare(QString(kSelectConversation) + "ORDER BY c.updated_at DESC, c.id DESC");
  while (true) {
    auto step = s.step();
    if (!step || !*step)
      break;
    list.append(readConversation(s));
  }
  return list;
}

std::optional<ConversationRow> ConversationRepository::conversation(int id) const {
  SqliteStatement s = m_db.prepare(QString(kSelectConversation) + "WHERE c.id = ?");
  (void)s.bind(1, id);
  auto step = s.step();
  if (!step || !*step)
    return std::nullopt;
  return readConversation(s);
}

std::expected<int, QString> ConversationRepository::create(const QString &title,
                                                           const QString &backend,
                                                           const QString &model) {
  SqliteStatement s = m_db.prepare(
      "INSERT INTO Conversations (title, backend, model) VALUES (?, ?, ?)");
  if (!s.isValid())
    return Error(lastError(m_db));
  (void)s.bind(1, title);
  (void)s.bind(2, backend);
  (void)s.bind(3, model);
  if (auto r = s.exec(); !r)
    return Error(r.error());
  return int(m_db.lastInsertRowId());
}

std::expected<void, QString> ConversationRepository::rename(int id, const QString &title) {
  SqliteStatement s = m_db.prepare("UPDATE Conversations SET title = ? WHERE id = ?");
  (void)s.bind(1, title);
  (void)s.bind(2, id);
  if (auto r = s.exec(); !r)
    return Error(r.error());
  return {};
}

std::expected<void, QString> ConversationRepository::setAgent(int id, const QString &backend,
                                                              const QString &model,
                                                              const QString &agentSession) {
  SqliteStatement s = m_db.prepare(
      "UPDATE Conversations SET backend = ?, model = ?, agent_session = ? WHERE id = ?");
  (void)s.bind(1, backend);
  (void)s.bind(2, model);
  (void)s.bind(3, agentSession);
  (void)s.bind(4, id);
  if (auto r = s.exec(); !r)
    return Error(r.error());
  return {};
}

std::expected<void, QString> ConversationRepository::remove(const QList<int> &ids) {
  if (ids.isEmpty())
    return {};
  if (auto r = m_db.transaction(); !r)
    return Error(r.error());
  SqliteStatement messages =
      m_db.prepare("DELETE FROM ConversationMessages WHERE conversation_id = ?");
  SqliteStatement conversations = m_db.prepare("DELETE FROM Conversations WHERE id = ?");
  for (int id : ids) {
    for (SqliteStatement *s : {&messages, &conversations}) {
      s->resetAndClearBindings();
      (void)s->bind(1, id);
      if (auto r = s->exec(); !r) {
        (void)m_db.rollback();
        return Error(r.error());
      }
    }
  }
  if (auto r = m_db.commit(); !r) {
    (void)m_db.rollback();
    return Error(r.error());
  }
  return {};
}

QList<ConversationMessage> ConversationRepository::messages(int conversationId) const {
  QList<ConversationMessage> list;
  SqliteStatement s = m_db.prepare(
      "SELECT id, role, content, created_at FROM ConversationMessages "
      "WHERE conversation_id = ? ORDER BY id");
  (void)s.bind(1, conversationId);
  while (true) {
    auto step = s.step();
    if (!step || !*step)
      break;
    ConversationMessage m;
    m.id = s.columnInt(0);
    m.role = roleFromName(s.columnText(1));
    m.content = s.columnText(2);
    m.createdAt = QDateTime::fromString(s.columnText(3), "yyyy-MM-dd HH:mm:ss");
    m.createdAt.setTimeZone(QTimeZone::UTC);
    list.append(m);
  }
  return list;
}

std::expected<int, QString> ConversationRepository::addMessage(int conversationId,
                                                               ConversationMessage::Role role,
                                                               const QString &content) {
  SqliteStatement s = m_db.prepare(
      "INSERT INTO ConversationMessages (conversation_id, role, content) VALUES (?, ?, ?)");
  if (!s.isValid())
    return Error(lastError(m_db));
  (void)s.bind(1, conversationId);
  (void)s.bind(2, conversationRoleName(role));
  (void)s.bind(3, content);
  if (auto r = s.exec(); !r)
    return Error(r.error());
  const int id = int(m_db.lastInsertRowId());
  SqliteStatement touch = m_db.prepare(
      "UPDATE Conversations SET updated_at = CURRENT_TIMESTAMP WHERE id = ?");
  (void)touch.bind(1, conversationId);
  if (auto r = touch.exec(); !r)
    return Error(r.error());
  return id;
}
