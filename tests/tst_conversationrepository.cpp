// Unit tests for ConversationRepository: the conversation tables of a
// project database.

#include "conversationrepository.h"
#include "projectstore.h"
#include "sqlitedb.h"

#include <QTemporaryDir>
#include <QtTest>

class TestConversationRepository : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();
  void createAndList();
  void messages();
  void renameAndAgent();
  void remove();

private:
  QTemporaryDir *m_dir = nullptr;
  SqliteDb m_db;
};

void TestConversationRepository::init() {
  m_dir = new QTemporaryDir;
  QVERIFY(ProjectStore::open(m_db, m_dir->filePath("P.db")).has_value());
}

void TestConversationRepository::cleanup() {
  m_db.close();
  delete m_dir;
  m_dir = nullptr;
}

void TestConversationRepository::createAndList() {
  ConversationRepository repo(m_db);
  QVERIFY(repo.conversations().isEmpty());
  const auto a = repo.create("Absenteeism", "Claude Code", "haiku");
  const auto b = repo.create("Supergames", "Pi", "UCL/Qwen3.8");
  QVERIFY(a && b);
  const auto list = repo.conversations();
  QCOMPARE(list.size(), 2);
  QCOMPARE(list[0].id, *b); // same time: the newest first
  QCOMPARE(list[1].title, QString("Absenteeism"));
  QCOMPARE(list[1].backend, QString("Claude Code"));
  QCOMPARE(list[1].model, QString("haiku"));
  QVERIFY(list[1].createdAt.isValid());
  QVERIFY(!repo.conversation(9999));
}

void TestConversationRepository::messages() {
  ConversationRepository repo(m_db);
  const int id = *repo.create("Q", "Claude Code", "");
  QVERIFY(repo.addMessage(id, ConversationMessage::Role::User, "What is the rate?"));
  QVERIFY(repo.addMessage(id, ConversationMessage::Role::Assistant,
                          "4.2 % [@aaaaaaaaaaaa:p4]."));
  QVERIFY(repo.addMessage(id, ConversationMessage::Role::Error, "limit reached"));
  const auto list = repo.messages(id);
  QCOMPARE(list.size(), 3);
  QCOMPARE(list[0].role, ConversationMessage::Role::User);
  QCOMPARE(list[1].role, ConversationMessage::Role::Assistant);
  QCOMPARE(list[1].content, QString("4.2 % [@aaaaaaaaaaaa:p4]."));
  QCOMPARE(list[2].role, ConversationMessage::Role::Error);
  QCOMPARE(repo.conversation(id)->messageCount, 3);
  // A message of an unknown conversation is refused (foreign key off, but
  // the role check is enforced).
  QVERIFY(!m_db.exec("INSERT INTO ConversationMessages (conversation_id, role, "
                     "content) VALUES (1, 'system', 'x')"));
}

void TestConversationRepository::renameAndAgent() {
  ConversationRepository repo(m_db);
  const int id = *repo.create("", "Claude Code", "");
  QVERIFY(repo.rename(id, "Peer effects"));
  QVERIFY(repo.setAgent(id, "Qwen", "qwen-b", "session-42"));
  const auto c = repo.conversation(id);
  QCOMPARE(c->title, QString("Peer effects"));
  QCOMPARE(c->backend, QString("Qwen"));
  QCOMPARE(c->model, QString("qwen-b"));
  QCOMPARE(c->agentSession, QString("session-42"));
}

void TestConversationRepository::remove() {
  ConversationRepository repo(m_db);
  const int a = *repo.create("A", "", "");
  const int b = *repo.create("B", "", "");
  QVERIFY(repo.addMessage(a, ConversationMessage::Role::User, "x"));
  QVERIFY(repo.addMessage(b, ConversationMessage::Role::User, "y"));
  QVERIFY(repo.remove({a}));
  QCOMPARE(repo.conversations().size(), 1);
  QVERIFY(repo.messages(a).isEmpty()); // its messages are gone too
  QCOMPARE(repo.messages(b).size(), 1);
}

QTEST_GUILESS_MAIN(TestConversationRepository)
#include "tst_conversationrepository.moc"
