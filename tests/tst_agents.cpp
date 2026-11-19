// Unit tests for the agent connectors (src/agents) and the Agent backend,
// against fake agents (no Claude, no network): fake_acp_agent (ACP),
// fake_claude (Claude Code stream-json), fake_pi (Pi RPC).

#include "acpconnector.h"
#include "agentbackend.h"
#include "agentregistry.h"
#include "agentsync.h"
#include "claudeconnector.h"
#include "piconnector.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace Agents;

namespace {

QString quoted(const char *path) { return "\"" + QString::fromLocal8Bit(path) + "\""; }

Profile profile(const QString &connector, const char *program) {
  Profile p;
  p.id = "fake-" + connector;
  p.name = "Fake " + connector;
  p.connector = connector;
  p.command = quoted(program);
  p.loginHint = "fake login";
  return p;
}

SessionOptions classic(const QString &dir, const QString &model = {}) {
  SessionOptions o;
  o.workDir = dir;
  o.systemPrompt = "SYSTEM PROMPT";
  o.model = model;
  return o;
}

Prompt ask(const QString &text) { return {PromptPart::textPart(text)}; }

/// One turn with a new connection: the result, or the opening error.
std::expected<TurnResult, QString> run(const Profile &p, const SessionOptions &o,
                                       const QString &text) {
  std::unique_ptr<Connection> c(Registry::instance().create(p));
  if (!c)
    return std::unexpected(QStringLiteral("no connector"));
  if (auto opened = openSync(*c, o, 20000); !opened)
    return std::unexpected(opened.error());
  TurnResult r = promptSync(*c, ask(text), 20000);
  c->close();
  return r;
}

QList<QJsonObject> loggedMessages(const QString &path) {
  QList<QJsonObject> out;
  QFile f(path);
  if (f.open(QIODevice::ReadOnly))
    for (const QByteArray &line : f.readAll().split('\n'))
      if (line.startsWith('{'))
        out.append(QJsonDocument::fromJson(line).object());
  return out;
}

QStringList loggedArgs(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return {};
  QStringList args;
  for (const QJsonValue &v : QJsonDocument::fromJson(f.readLine()).array())
    args << v.toString();
  return args;
}

QJsonObject find(const QList<QJsonObject> &messages, const QString &method) {
  for (const QJsonObject &m : messages)
    if (m.value("method").toString() == method)
      return m;
  return {};
}

} // namespace

class TestAgents : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  // Registry and profiles
  void builtins();
  void parseProfile();
  void userProfileOverrides();
  void splitCommand();
  void availability();

  // ACP
  void acpSessionParams();
  void acpAnswer();
  void acpModelByConfigOption();
  void acpModelsStyle();
  void acpPermissionRefused();
  void acpCancel();
  void acpTimeout();
  void acpNotLoggedIn();
  void acpCannotStart();
  void acpListModels();

  // Claude Code (stream-json)
  void claudeArguments();
  void claudeAnswer();
  void claudeLimit();
  void claudeCancel();
  void claudeCancelKills();

  // Pi (RPC)
  void piArguments();
  void piAnswer();
  void piError();
  void piCancel();
  void piListModels();

  // The Agent backend
  void backendPrompt();
  void backendGenerate();
  void backendUnknownAgent();

private:
  QTemporaryDir *m_dir = nullptr;
  QString m_log;
};

void TestAgents::init() {
  m_dir = new QTemporaryDir;
  m_log = m_dir->filePath("agent.log");
  qputenv("FAKE_ACP_LOG", m_log.toLocal8Bit());
  qputenv("FAKE_AGENT_LOG", m_log.toLocal8Bit());
  qunsetenv("FAKE_ACP_AUTH_FAIL");
  qunsetenv("FAKE_ACP_STYLE");
}

void TestAgents::cleanup() {
  qunsetenv("FAKE_ACP_LOG");
  qunsetenv("FAKE_AGENT_LOG");
  delete m_dir;
  m_dir = nullptr;
}

/* ── Registry ──────────────────────────────────────────────────── */

// One connector per folder of src/agents/connectors, and the app's profiles.
void TestAgents::builtins() {
  const Registry &r = Registry::instance();
  for (const char *id : {"acp", "claude", "pi"})
    QVERIFY2(r.connector(id), id);
  for (const char *id : {"claude", "claude-acp", "qwen", "pi", "gemini", "antigravity"}) {
    const auto p = r.profile(id);
    QVERIFY2(p, id);
    QVERIFY2(r.connector(p->connector), id); // every profile has its connector
  }
  QCOMPARE(r.profile("claude")->connector, QString("claude"));
  QVERIFY(r.profile("qwen")->verified);
  QVERIFY(!r.profile("gemini")->verified);
}

void TestAgents::parseProfile() {
  QVERIFY(!Registry::parseProfile({{"id", "x"}, {"connector", "acp"}}));
  const auto p = Registry::parseProfile(
      {{"id", "x"}, {"connector", "acp"}, {"command", "x --acp"},
       {"models", QJsonArray{"a", "b"}}, {"env", QJsonObject{{"K", "V"}}},
       {"options", QJsonObject{{"sessionMeta", QJsonObject{{"m", 1}}}}}});
  QVERIFY(p);
  QCOMPARE(p->name, QString("x")); // the id when no name
  QCOMPARE(p->models, (QStringList{"a", "b"}));
  QCOMPARE(p->env.value("K"), QString("V"));
  QVERIFY(p->options.value("sessionMeta").isObject());
}

// A user's JSON file adds an agent, or replaces a built-in one.
void TestAgents::userProfileOverrides() {
  Registry r;
  r.addProfile(*Registry::parseProfile({{"id", "qwen"}, {"connector", "acp"},
                                        {"command", "qwen --acp"}}));
  QFile f(m_dir->filePath("qwen.json"));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(R"({"id": "qwen", "connector": "acp", "command": "/opt/qwen --acp"})");
  f.close();
  QFile bad(m_dir->filePath("bad.json"));
  QVERIFY(bad.open(QIODevice::WriteOnly));
  bad.write("{");
  bad.close();
  const QStringList failed = r.loadProfiles(m_dir->path());
  QCOMPARE(failed.size(), 1);
  QCOMPARE(r.profiles().size(), 1);
  QCOMPARE(r.profile("qwen")->command, QString("/opt/qwen --acp"));
}

void TestAgents::splitCommand() {
#ifndef Q_OS_WIN
  QCOMPARE(AgentProcess::splitCommand("\"/opt/my agent/x\" --acp"),
           (QStringList{"/opt/my agent/x", "--acp"}));
#endif
  QVERIFY(AgentProcess::splitCommand("  ").isEmpty());
}

// Installed or not, a runtime missing or not, and the message that says so.
void TestAgents::availability() {
  Profile p = profile("acp", FAKE_ACP_AGENT);
  p.name = "Fake";
  QVERIFY(Agents::availability(p).ok());

  p.command = "/no/such/agent --acp";
  p.installHint = "get it";
  QVERIFY(!Agents::availability(p).installed);
  QString message = startFailureMessage(p, "process error");
  QVERIFY2(message.contains("not installed") && message.contains("get it"), qPrintable(message));

  // A Node.js agent where there is no Node.js (empty PATH).
  p.command = quoted(FAKE_ACP_AGENT);
  p.runtimes = {"node"};
  const QByteArray path = qgetenv("PATH");
  qputenv("PATH", m_dir->path().toLocal8Bit());
  const Availability a = Agents::availability(p);
  message = startFailureMessage(p, "process error");
  qputenv("PATH", path);
  QVERIFY(a.installed);
  QCOMPARE(a.missingRuntimes, QStringList{"node"});
  QVERIFY2(message.contains("needs Node.js") && message.contains("nodejs.org"),
           qPrintable(message));

  // The built-in Node.js agents say so.
  QCOMPARE(Registry::instance().profile("qwen")->runtimes, QStringList{"node"});
  QVERIFY(Registry::instance().profile("claude")->runtimes.isEmpty());
}

/* ── ACP ───────────────────────────────────────────────────────── */

void TestAgents::acpSessionParams() {
  const QJsonObject meta{{"claudeCode", QJsonObject{{"options", QJsonObject{}}}}};
  SessionOptions o = classic("/tmp");
  o.mcpServers.append({"hodeia", "/opt/Hodeia", {"--mcp"}, {{"K", "V"}}});
  QJsonObject p = AcpConnection::sessionParams(o, meta, true);
  QCOMPARE(p.value("_meta").toObject(), meta);
  QCOMPARE(p.value("systemPrompt").toObject().value("mode").toString(), QString("override"));
  const QJsonObject server = p.value("mcpServers").toArray()[0].toObject();
  QCOMPARE(server.value("args").toArray()[0].toString(), QString("--mcp"));
  QCOMPARE(server.value("env").toArray()[0].toObject().value("name").toString(), QString("K"));

  // Tools on: no restriction; no override support: no systemPrompt.
  o.tools = true;
  p = AcpConnection::sessionParams(o, meta, false);
  QVERIFY(!p.contains("_meta"));
  QVERIFY(!p.contains("systemPrompt"));
}

void TestAgents::acpAnswer() {
  const auto r = run(profile("acp", FAKE_ACP_AGENT), classic(m_dir->path()), "Hello agent");
  QVERIFY2(r, r ? "" : qPrintable(r.error()));
  QCOMPARE(r->stop, TurnResult::Stop::EndTurn);
  QCOMPARE(r->text, QString("Echo: Hello agent"));
  const auto messages = loggedMessages(m_log);
  QCOMPARE(find(messages, "initialize").value("params").toObject().value("protocolVersion").toInt(), 1);
  const QJsonObject session = find(messages, "session/new").value("params").toObject();
  QCOMPARE(session.value("systemPrompt").toObject().value("content").toArray()[0]
               .toObject().value("text").toString(),
           QString("SYSTEM PROMPT"));
  QVERIFY(find(messages, "session/set_config_option").isEmpty()); // no model asked
}

void TestAgents::acpModelByConfigOption() {
  QVERIFY(run(profile("acp", FAKE_ACP_AGENT), classic(m_dir->path(), "sonnet"), "Hi"));
  const QJsonObject set =
      find(loggedMessages(m_log), "session/set_config_option").value("params").toObject();
  QCOMPARE(set.value("configId").toString(), QString("model"));
  QCOMPARE(set.value("value").toString(), QString("sonnet"));

  const auto refused =
      run(profile("acp", FAKE_ACP_AGENT), classic(m_dir->path(), "unknown-model"), "Hi");
  QVERIFY(!refused);
  QVERIFY(refused.error().contains("unknown-model"));
}

// Like Qwen Code: `models` + session/set_model, and the system prompt goes
// at the top of the first prompt.
void TestAgents::acpModelsStyle() {
  qputenv("FAKE_ACP_STYLE", "models");
  const auto r = run(profile("acp", FAKE_ACP_AGENT), classic(m_dir->path(), "qwen-b"), "Question");
  QVERIFY2(r, r ? "" : qPrintable(r.error()));
  QVERIFY2(r->text.startsWith("Echo: SYSTEM PROMPT"), qPrintable(r->text));
  QVERIFY(r->text.contains("Question"));
  const auto messages = loggedMessages(m_log);
  QCOMPARE(find(messages, "session/set_model").value("params").toObject().value("modelId").toString(),
           QString("qwen-b"));
  QVERIFY(!find(messages, "session/new").value("params").toObject().contains("systemPrompt"));
}

void TestAgents::acpPermissionRefused() {
  const auto r = run(profile("acp", FAKE_ACP_AGENT), classic(m_dir->path()), "PERMISSION");
  QVERIFY(r);
  QCOMPARE(r->text, QString("Permission: selected no"));
}

void TestAgents::acpCancel() {
  std::unique_ptr<Connection> c(Registry::instance().create(profile("acp", FAKE_ACP_AGENT)));
  QVERIFY(openSync(*c, classic(m_dir->path()), 20000));
  QTimer::singleShot(300, c.get(), [&]() { c->cancel(); });
  const TurnResult r = promptSync(*c, ask("SLOW"), 20000);
  QCOMPARE(r.stop, TurnResult::Stop::Cancelled);
  QCOMPARE(r.text, QString("Partial"));
  QVERIFY(!find(loggedMessages(m_log), "session/cancel").isEmpty());
}

void TestAgents::acpTimeout() {
  std::unique_ptr<Connection> c(Registry::instance().create(profile("acp", FAKE_ACP_AGENT)));
  QVERIFY(openSync(*c, classic(m_dir->path()), 20000));
  const TurnResult r = promptSync(*c, ask("SLOW"), 500);
  QCOMPARE(r.stop, TurnResult::Stop::Error);
  QCOMPARE(r.error, QString("timeout"));
}

void TestAgents::acpNotLoggedIn() {
  qputenv("FAKE_ACP_AUTH_FAIL", "1");
  const auto r = run(profile("acp", FAKE_ACP_AGENT), classic(m_dir->path()), "Hi");
  QVERIFY(!r);
  QVERIFY2(r.error().contains("not logged in") && r.error().contains("fake login"),
           qPrintable(r.error()));
}

void TestAgents::acpCannotStart() {
  Profile p = profile("acp", FAKE_ACP_AGENT);
  p.command = "/no/such/agent";
  p.installHint = "install it";
  const auto r = run(p, classic(m_dir->path()), "Hi");
  QVERIFY(!r);
  QVERIFY2(r.error().contains("install it"), qPrintable(r.error()));
}

void TestAgents::acpListModels() {
  const auto models = Registry::instance().connector("acp")->listModels(profile("acp", FAKE_ACP_AGENT));
  QVERIFY2(models, models ? "" : qPrintable(models.error()));
  QCOMPARE(*models, (QStringList{"default", "sonnet"}));
  qputenv("FAKE_ACP_STYLE", "models");
  QCOMPARE(*Registry::instance().connector("acp")->listModels(profile("acp", FAKE_ACP_AGENT)),
           (QStringList{"qwen-a", "qwen-b"}));
}

/* ── Claude Code ───────────────────────────────────────────────── */

void TestAgents::claudeArguments() {
  QStringList args = ClaudeConnection::arguments(classic("/tmp", "haiku"));
  QVERIFY(args.contains("stream-json"));
  QCOMPARE(args.at(args.indexOf("--model") + 1), QString("haiku"));
  QCOMPARE(args.at(args.indexOf("--system-prompt") + 1), QString("SYSTEM PROMPT"));
  QCOMPARE(args.at(args.indexOf("--tools") + 1), QString(""));
  QCOMPARE(args.at(args.indexOf("--setting-sources") + 1), QString(""));
  QVERIFY(args.contains("--strict-mcp-config"));
  QVERIFY(!args.contains("--mcp-config"));

  SessionOptions agentic = classic("/tmp", "default");
  agentic.tools = true;
  agentic.mcpServers.append({"hodeia", "/opt/Hodeia", {"--mcp"}, {}});
  args = ClaudeConnection::arguments(agentic);
  QVERIFY(!args.contains("--model")); // "default": the account's
  QVERIFY(!args.contains("--tools"));
  const QJsonObject mcp =
      QJsonDocument::fromJson(args.at(args.indexOf("--mcp-config") + 1).toUtf8()).object();
  QCOMPARE(mcp.value("mcpServers").toObject().value("hodeia").toObject().value("command").toString(),
           QString("/opt/Hodeia"));
}

void TestAgents::claudeAnswer() {
  Profile p = profile("claude", FAKE_CLAUDE);
  const auto r = run(p, classic(m_dir->path(), "haiku"), "Hello Claude");
  QVERIFY2(r, r ? "" : qPrintable(r.error()));
  QCOMPARE(r->stop, TurnResult::Stop::EndTurn);
  QCOMPARE(r->text, QString("Echo: Hello Claude"));
  const QStringList args = loggedArgs(m_log);
  QCOMPARE(args.at(args.indexOf("--model") + 1), QString("haiku"));
}

// At its usage limit Claude Code answers with an error result: an error,
// not an answer.
void TestAgents::claudeLimit() {
  const auto r = run(profile("claude", FAKE_CLAUDE), classic(m_dir->path()), "LIMIT");
  QVERIFY(r);
  QCOMPARE(r->stop, TurnResult::Stop::Error);
  QVERIFY(r->text.isEmpty());
  QVERIFY(r->error.contains("limit"));
}

void TestAgents::claudeCancel() {
  std::unique_ptr<Connection> c(Registry::instance().create(profile("claude", FAKE_CLAUDE)));
  QVERIFY(openSync(*c, classic(m_dir->path()), 20000));
  QTimer::singleShot(300, c.get(), [&]() { c->cancel(); });
  const TurnResult r = promptSync(*c, ask("SLOW"), 20000);
  QCOMPARE(r.stop, TurnResult::Stop::Cancelled);
  QCOMPARE(r.text, QString("Partial"));
}

// A turn that ignores the interrupt: the process is killed.
void TestAgents::claudeCancelKills() {
  std::unique_ptr<Connection> c(Registry::instance().create(profile("claude", FAKE_CLAUDE)));
  QVERIFY(openSync(*c, classic(m_dir->path()), 20000));
  QTimer::singleShot(300, c.get(), [&]() { c->cancel(); });
  QElapsedTimer t;
  t.start();
  const TurnResult r = promptSync(*c, ask("HANG"), 30000);
  QCOMPARE(r.stop, TurnResult::Stop::Cancelled);
  QVERIFY(t.elapsed() < 15000);
}

/* ── Pi ────────────────────────────────────────────────────────── */

void TestAgents::piArguments() {
  QStringList args = PiConnection::arguments(classic("/tmp", "UCL/Qwen3.8"));
  QCOMPARE(args.mid(0, 3), (QStringList{"--mode", "rpc", "--no-session"}));
  QCOMPARE(args.at(args.indexOf("--model") + 1), QString("UCL/Qwen3.8"));
  for (const char *flag : {"--no-tools", "--no-extensions", "--no-skills",
                           "--no-prompt-templates", "--no-context-files", "--no-mcp"})
    QVERIFY2(args.contains(flag), flag);
}

void TestAgents::piAnswer() {
  const auto r = run(profile("pi", FAKE_PI), classic(m_dir->path()), "Hello Pi");
  QVERIFY2(r, r ? "" : qPrintable(r.error()));
  QCOMPARE(r->stop, TurnResult::Stop::EndTurn);
  QCOMPARE(r->text, QString("Echo: Hello Pi"));
  const QStringList args = loggedArgs(m_log);
  QCOMPARE(args.at(args.indexOf("--system-prompt") + 1), QString("SYSTEM PROMPT"));
}

void TestAgents::piError() {
  const auto r = run(profile("pi", FAKE_PI), classic(m_dir->path()), "ERROR");
  QVERIFY(r);
  QCOMPARE(r->stop, TurnResult::Stop::Error);
  QCOMPARE(r->error, QString("provider is down"));
}

void TestAgents::piCancel() {
  std::unique_ptr<Connection> c(Registry::instance().create(profile("pi", FAKE_PI)));
  QVERIFY(openSync(*c, classic(m_dir->path()), 20000));
  QTimer::singleShot(300, c.get(), [&]() { c->cancel(); });
  const TurnResult r = promptSync(*c, ask("SLOW"), 20000);
  QCOMPARE(r.stop, TurnResult::Stop::Cancelled);
  QCOMPARE(r.text, QString("Partial"));
}

void TestAgents::piListModels() {
  const auto models = Registry::instance().connector("pi")->listModels(profile("pi", FAKE_PI));
  QVERIFY2(models, models ? "" : qPrintable(models.error()));
  QCOMPARE(*models, (QStringList{"P/m1", "P/m2"}));
}

/* ── Agent backend ─────────────────────────────────────────────── */

// The Anthropic layout, as prompt parts (text, then images).
void TestAgents::backendPrompt() {
  PromptPayload p;
  p.userMessage = "Question?";
  p.textContext = "CONTEXT";
  PageImage img;
  img.width = 16;
  img.height = 16;
  img.data.assign(16 * 16 * 3, 128);
  p.visualContext.push_back(img);
  const Prompt parts = AgentBackend::prompt(p);
  QCOMPARE(parts.size(), 2);
  QVERIFY(parts[0].text.startsWith("Question?") && parts[0].text.contains("CONTEXT"));
  QVERIFY(parts[1].isImage() || parts[1].mimeType == "image/jpeg");
  QCOMPARE(parts[1].mimeType, QString("image/jpeg"));
}

// The built-in "claude" profile, its command replaced by the fake.
void TestAgents::backendGenerate() {
  const QVariantMap config{{"api", "agent"}, {"agent", "claude"}, {"url", quoted(FAKE_CLAUDE)}};
  PromptPayload p;
  p.systemPrompt = "SYSTEM";
  p.userMessage = "Hello backend";
  const auto out = AgentBackend::make("sonnet", config)->generate(p, {}, nullptr);
  QVERIFY2(out, out ? "" : qPrintable(out.error().detail));
  QVERIFY2(out->startsWith("Echo: Hello backend"), qPrintable(*out));
  const QStringList args = loggedArgs(m_log);
  QCOMPARE(args.at(args.indexOf("--model") + 1), QString("sonnet"));
  QCOMPARE(args.at(args.indexOf("--system-prompt") + 1), QString("SYSTEM"));

  const auto limit = [&]() {
    PromptPayload q;
    q.userMessage = "LIMIT";
    return AgentBackend::make("", config)->generate(q, {}, nullptr);
  }();
  QVERIFY(!limit);
  QCOMPARE(limit.error().kind, GenerationError::Kind::RequestFailed);
}

void TestAgents::backendUnknownAgent() {
  const auto out = AgentBackend::make("", {{"api", "agent"}, {"agent", "nope"}})
                       ->generate(PromptPayload{}, {}, nullptr);
  QVERIFY(!out);
  QVERIFY(out.error().detail.contains("nope"));
}

QTEST_GUILESS_MAIN(TestAgents)
#include "tst_agents.moc"
