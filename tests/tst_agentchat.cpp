// Unit tests for AgentChatController (mode agentique of the Query window),
// with the fake `claude` as agent.  The app's settings and projects live in
// QStandardPaths' test location, never the user's.

#include "agentchatcontroller.h"
#include "configmanager.h"
#include "database.h"
#include "sqlitedb.h"
#include "sqlitevec.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {

const QString kHash = "aaaaaaaaaaaa1111111111111111111a";

QStringList loggedArgs(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return {};
  QStringList args;
  for (const QJsonValue &v : QJsonDocument::fromJson(f.readLine()).array())
    args << v.toString();
  return args;
}

int loggedUserMessages(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return 0;
  return int(f.readAll().count("\"type\":\"user\""));
}

} // namespace

class TestAgentChat : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void init();
  void cleanup();
  void cleanupTestCase();
  void conversation();
  void resume();
  void transcriptWhenNotResumed();
  void resumeFails();
  void citations();
  void errorsAndStop();
  void backends();

private:
  /// Send and wait for the end of the turn.
  void sendAndWait(const QString &text);

  QTemporaryDir *m_logDir = nullptr;
  QString m_log;
  ConfigManager *m_config = nullptr;
  SqliteVecManager *m_embeddings = nullptr;
  DatabaseManager *m_database = nullptr;
  AgentChatController *m_chat = nullptr;
};

void TestAgentChat::initTestCase() {
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setApplicationName("HodeiaTestAgentChat");
  QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
}

void TestAgentChat::cleanupTestCase() {
  QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
}

void TestAgentChat::init() {
  m_logDir = new QTemporaryDir;
  m_log = m_logDir->filePath("agent.log");
  qputenv("FAKE_AGENT_LOG", m_log.toLocal8Bit());
  qunsetenv("FAKE_CLAUDE_SESSION");
  qputenv("FAKE_ACP_LOG", m_log.toLocal8Bit());
  m_config = new ConfigManager;
  m_config->setAgentBackend("Fake Claude", "claude", "\"" + QString(FAKE_CLAUDE) + "\"");
  m_config->setAgentBackend("Fake Pi", "pi", "\"" + QString(FAKE_PI) + "\"");
  m_config->setAgentBackend("Fake ACP", "qwen", "\"" + QString(FAKE_ACP_AGENT) + "\"");
  m_embeddings = new SqliteVecManager(m_config);
  m_database = new DatabaseManager(m_config, *m_embeddings);
  QVERIFY(m_database->isOpen());
  m_chat = new AgentChatController(m_config, m_database);
}

void TestAgentChat::cleanup() {
  delete m_chat;
  delete m_database;
  delete m_embeddings;
  delete m_config;
  qunsetenv("FAKE_AGENT_LOG");
  delete m_logDir;
  // A fresh project for the next test.
  QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
}

void TestAgentChat::sendAndWait(const QString &text) {
  m_chat->send(text);
  QVERIFY(m_chat->busy());
  QTRY_VERIFY_WITH_TIMEOUT(!m_chat->busy(), 20000);
}

void TestAgentChat::conversation() {
  m_chat->newConversation("Fake Claude", "haiku");
  sendAndWait("Hello agent");
  QCOMPARE(m_chat->currentId() > 0, true);
  QVariantList messages = m_chat->messages();
  QCOMPARE(messages.size(), 2);
  QCOMPARE(messages[0].toMap().value("role").toString(), QString("user"));
  QCOMPARE(messages[1].toMap().value("text").toString(), QString("Echo: Hello agent"));
  const QVariantList list = m_chat->conversations();
  QCOMPARE(list.size(), 1);
  QCOMPARE(list[0].toMap().value("title").toString(), QString("Hello agent"));
  QCOMPARE(list[0].toMap().value("model").toString(), QString("haiku"));

  // The agent gets HodeIA's MCP server, its tools allowed, nothing else.
  const QStringList args = loggedArgs(m_log);
  QCOMPARE(args.at(args.indexOf("--model") + 1), QString("haiku"));
  QCOMPARE(args.at(args.indexOf("--tools") + 1), QString(""));
  QVERIFY(args.at(args.indexOf("--allowedTools") + 1).contains("mcp__hodeia__search_library"));
  const QJsonObject mcp =
      QJsonDocument::fromJson(args.at(args.indexOf("--mcp-config") + 1).toUtf8()).object();
  QCOMPARE(mcp.value("mcpServers").toObject().value("hodeia").toObject().value("args").toArray()[0]
               .toString(),
           QString("--mcp"));
  QVERIFY(args.at(args.indexOf("--system-prompt") + 1).contains("[@95f8ed154398:p12]"));

  // A second message goes to the same agent process.
  sendAndWait("And then?");
  QCOMPARE(m_chat->messages().size(), 4);
  QCOMPARE(loggedUserMessages(m_log), 2);
  QVERIFY(!args.contains("--resume"));
}

// A conversation opened again resumes the agent's session.
void TestAgentChat::resume() {
  m_chat->newConversation("Fake Claude", "");
  sendAndWait("First");
  const int id = m_chat->currentId();
  m_chat->newConversation("Fake Claude", "");
  m_chat->openConversation(id);
  QFile::remove(m_log);
  sendAndWait("Second");
  const QStringList args = loggedArgs(m_log);
  QCOMPARE(args.at(args.indexOf("--resume") + 1), QString("fake-claude"));
  // Resumed: no transcript in front of the message.
  QCOMPARE(m_chat->messages().last().toMap().value("text").toString(), QString("Echo: Second"));
}

// An agent that cannot resume sessions (this fake ACP agent): it gets the
// conversation so far.
void TestAgentChat::transcriptWhenNotResumed() {
  m_chat->newConversation("Fake ACP", "");
  sendAndWait("Remember 4.2");
  const int id = m_chat->currentId();
  m_chat->newConversation("Fake ACP", "");
  m_chat->openConversation(id);
  sendAndWait("Which rate?");
  const QString answer = m_chat->messages().last().toMap().value("text").toString();
  QVERIFY2(answer.startsWith("Echo: Our conversation so far"), qPrintable(answer));
  QVERIFY(answer.contains("User: Remember 4.2"));
  QVERIFY(answer.contains("Which rate?"));
}

// The agent does not have the session any more: a new one, with the story.
void TestAgentChat::resumeFails() {
  m_chat->newConversation("Fake Claude", "");
  sendAndWait("Remember 4.2");
  const int id = m_chat->currentId();
  m_chat->newConversation("Fake Claude", "");
  m_chat->openConversation(id);
  qputenv("FAKE_CLAUDE_RESUME_FAIL", "1");
  sendAndWait("Which rate?");
  qunsetenv("FAKE_CLAUDE_RESUME_FAIL");
  const QVariantList messages = m_chat->messages();
  QCOMPARE(messages.size(), 4); // no error message: retried
  const QString answer = messages.last().toMap().value("text").toString();
  QVERIFY2(answer.startsWith("Echo: Our conversation so far"), qPrintable(answer));
}

// The marks of the agent become citations of the library's papers.
void TestAgentChat::citations() {
  SqliteStatement insert = m_database->db().prepare(
      "INSERT INTO chunks (embedding, paper_hash, page, chunk_idx, chunk_text, citation, "
      "clean_title, page_metadata) VALUES (vec_f32(?), ?, 4, 0, 'text', 'Smith (2020)', "
      "'Peer absence', '')");
  (void)insert.bind(1, QByteArray(384 * sizeof(float), '\0'));
  (void)insert.bind(2, kHash);
  QVERIFY(insert.exec());
  m_chat->newConversation("Fake Claude", "");
  sendAndWait("Rate [@aaaaaaaaaaaa:p4]");
  const QString answer = m_chat->messages().last().toMap().value("text").toString();
  QVERIFY2(answer.startsWith("Echo: Rate (Smith, 2020, p. 4)"), qPrintable(answer));
  QVERIFY(answer.contains("**Sources**"));
}

void TestAgentChat::errorsAndStop() {
  m_chat->newConversation("Fake Claude", "");
  sendAndWait("LIMIT");
  QVariantList messages = m_chat->messages();
  QCOMPARE(messages.last().toMap().value("role").toString(), QString("error"));
  QVERIFY(messages.last().toMap().value("text").toString().contains("limit"));

  m_chat->send("SLOW");
  QTRY_VERIFY_WITH_TIMEOUT(m_chat->status().contains("Writing"), 10000);
  m_chat->stop();
  QTRY_VERIFY_WITH_TIMEOUT(!m_chat->busy(), 20000);
  const QString last = m_chat->messages().last().toMap().value("text").toString();
  QVERIFY2(last.startsWith("Partial") && last.contains("stopped"), qPrintable(last));

  // An agent backend that does not exist any more: an error message, at once.
  m_chat->newConversation("Gone", "");
  m_chat->send("Hello");
  QVERIFY(!m_chat->busy());
  QCOMPARE(m_chat->messages().last().toMap().value("role").toString(), QString("error"));
}

// Only agents that can reach HodeIA's MCP server are offered (not Pi).
void TestAgentChat::backends() {
  QStringList names;
  for (const QVariant &b : m_chat->agentBackends())
    names << b.toMap().value("name").toString();
  QVERIFY(names.contains("Fake Claude"));
  QVERIFY(!names.contains("Fake Pi"));
}

QTEST_GUILESS_MAIN(TestAgentChat)
#include "tst_agentchat.moc"
