// Unit tests for the agent connectors (src/agents) and the Agent backend,
// against fake agents (no Claude, no network): fake_acp_agent (ACP),
// fake_claude (Claude Code stream-json), fake_codex (codex app-server).

#include "acpconnector.h"
#include "agentbackend.h"
#include "agentguard.h"
#include "agentregistry.h"
#include "agentsync.h"
#include "claudeconnector.h"
#include "codexconnector.h"

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
  void acpPermissionAnswer();
  void acpExpand();
  void acpProfileFiles();
  void acpSetup();
  void acpSetupFails();
  void guard();

  // Claude Code (stream-json)
  void claudeArguments();
  void claudeAnswer();
  void claudeLimit();
  void claudeCancel();
  void claudeCancelKills();
  void claudeStream();

  // Codex (app-server)
  void codexThreadParams();
  void codexAnswer();
  void codexStream();
  void codexTwoMessages();
  void codexError();
  void codexCancel();
  void codexCancelKills();
  void codexRefusesApprovals();
  void codexResume();
  void codexListModels();

  // The Agent backend
  void backendPrompt();
  void backendGenerate();
  void backendUnknownAgent();
  void guessAgent();

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
  for (const char *id : {"acp", "claude", "codex"})
    QVERIFY2(r.connector(id), id);
  for (const char *id : {"claude", "codex", "qwen", "antigravity", "hermes"}) {
    const auto p = r.profile(id);
    QVERIFY2(p, id);
    QVERIFY2(r.connector(p->connector), id); // every profile has its connector
  }
  QCOMPARE(r.profile("claude")->connector, QString("claude"));
  QCOMPARE(r.profile("codex")->connector, QString("codex"));
  QVERIFY(r.profile("qwen")->verified);
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

// Mode agentique: HodeIA's tools are allowed, whatever the agent calls them;
// anything else is refused.
void TestAgents::acpPermissionAnswer() {
  const QStringList allowed{"hodeia/search_library", "hodeia/get_page"};
  const QJsonArray options{QJsonObject{{"optionId", "yes"}, {"kind", "allow_once"}},
                           QJsonObject{{"optionId", "no"}, {"kind", "reject_once"}}};
  const auto ask = [&](const QJsonObject &call) {
    return AcpConnection::permissionAnswer({{"toolCall", call}, {"options", options}}, allowed);
  };
  QCOMPARE(ask({{"title", "mcp__hodeia__search_library"}}), QString("yes"));
  QCOMPARE(ask({{"title", "search_library (hodeia MCP Server)"}}), QString("yes"));
  QCOMPARE(ask({{"title", "mcp__other__search_library"}}), QString("no"));
  QCOMPARE(ask({{"title", "Write file"}, {"rawInput", QJsonObject{{"path", "/etc/x"}}}}),
           QString("no"));
  QCOMPARE(AcpConnection::permissionAnswer({{"toolCall", QJsonObject{{"title", "Edit"}}},
                                            {"options", options}},
                                           {}),
           QString("no")); // classic mode: nothing allowed
  // Reading the web: only with SessionOptions::web.
  const QJsonObject fetch{{"title", "WebFetch"}, {"kind", "fetch"}};
  QCOMPARE(ask(fetch), QString("no"));
  QCOMPARE(AcpConnection::permissionAnswer({{"toolCall", fetch}, {"options", options}}, allowed, true),
           QString("yes"));
  QCOMPARE(AcpConnection::permissionAnswer(
               {{"toolCall", QJsonObject{{"title", "rm -rf x"}, {"kind", "execute"}}},
                {"options", options}},
               allowed, true),
           QString("no"));
}

void TestAgents::acpExpand() {
  Profile p;
  p.command = "\"/opt/my agent/hermes\" acp";
  p.stateDir = "/state/hermes";
  p.guardCommand = {"/opt/Hodeia App/Hodeia", "--agent-guard"};
  const QJsonValue v = AcpConnection::expand(
      QJsonObject{{"${stateDir}/a.json", QJsonArray{"${program}", "x ${workDir}"}},
                  {"hook", "${guard} web_*"}},
      p, "/work");
  const QJsonObject o = v.toObject();
  QCOMPARE(o.keys(), (QStringList{"/state/hermes/a.json", "hook"}));
  QCOMPARE(o.value("/state/hermes/a.json").toArray(), (QJsonArray{"/opt/my agent/hermes", "x /work"}));
  QCOMPARE(o.value("hook").toString(), QString("\"/opt/Hodeia App/Hodeia\" --agent-guard web_*"));
}

// The profile's files are written (placeholders replaced) before the agent
// starts, and its env has the placeholders replaced too.
void TestAgents::acpProfileFiles() {
  Profile p = profile("acp", FAKE_ACP_AGENT);
  p.stateDir = m_dir->filePath("state");
  p.guardCommand = {"guard", "--agent-guard"};
  p.env.insert("FAKE_SETTINGS", "${stateDir}/settings.json");
  p.options["files"] = QJsonObject{
      {"${stateDir}/settings.json", QJsonObject{{"deny", QJsonArray{"shell"}}}},
      {"${workDir}/.agents/hooks.json", QJsonObject{{"command", "${guard} web"}}}};
  QVERIFY(run(p, classic(m_dir->path()), "Hi"));
  QFile settings(m_dir->filePath("state/settings.json"));
  QVERIFY(settings.open(QIODevice::ReadOnly));
  QCOMPARE(QJsonDocument::fromJson(settings.readAll()).object(),
           (QJsonObject{{"deny", QJsonArray{"shell"}}}));
  QFile hooks(m_dir->filePath(".agents/hooks.json"));
  QVERIFY(hooks.open(QIODevice::ReadOnly));
  QCOMPARE(QJsonDocument::fromJson(hooks.readAll()).object().value("command").toString(),
           QString("guard --agent-guard web"));
}

// Setup: "unless" skips a step when it succeeds; done once per session.
void TestAgents::acpSetup() {
  Profile p = profile("acp", FAKE_ACP_AGENT);
  p.id = "setup-test";
  p.stateDir = m_dir->filePath("state");
  const QString cmake = QString::fromLocal8Bit(CMAKE_PROGRAM);
  p.options["setup"] = QJsonArray{
      QJsonObject{{"unless", QJsonArray{cmake, "-E", "true"}},
                  {"run", QJsonArray{cmake, "-E", "touch", "${stateDir}/skipped"}}},
      QJsonObject{{"unless", QJsonArray{cmake, "-E", "false"}},
                  {"run", QJsonArray{cmake, "-E", "touch", "${stateDir}/ran"}}},
      QJsonObject{{"run", QJsonArray{cmake, "-E", "touch", "${stateDir}/always"}}}};
  QDir().mkpath(p.stateDir);
  QVERIFY(run(p, classic(m_dir->path()), "Hi"));
  QVERIFY(!QFile::exists(m_dir->filePath("state/skipped")));
  QVERIFY(QFile::exists(m_dir->filePath("state/ran")));
  QVERIFY(QFile::exists(m_dir->filePath("state/always")));
  QFile::remove(m_dir->filePath("state/always"));
  QVERIFY(run(p, classic(m_dir->path()), "Hi")); // already done
  QVERIFY(!QFile::exists(m_dir->filePath("state/always")));
}

// A setup that fails: the agent is not started, the error says why.
void TestAgents::acpSetupFails() {
  Profile p = profile("acp", FAKE_ACP_AGENT);
  p.id = "setup-fails";
  p.options["setup"] = QJsonArray{
      QJsonObject{{"run", QJsonArray{QString::fromLocal8Bit(CMAKE_PROGRAM), "-E", "false"}}}};
  const auto r = run(p, classic(m_dir->path()), "Hi");
  QVERIFY(!r);
  QVERIFY2(r.error().contains("Fake acp"), qPrintable(r.error()));
  QVERIFY(loggedMessages(m_log).isEmpty()); // never started
}

void TestAgents::guard() {
  const QStringList allowed{"search_web", "mcp_hodeia_*"};
  const auto decide = [&](const QString &name) {
    return guardDecision({{"toolCall", QJsonObject{{"name", name}}}}, allowed)
        .value("decision").toString();
  };
  QCOMPARE(decide("search_web"), QString("allow"));
  QCOMPARE(decide("mcp_hodeia_list_papers"), QString("allow"));
  QCOMPARE(decide("run_command"), QString("deny"));
  QCOMPARE(decide("write_file"), QString("deny"));
  QCOMPARE(decide("search_web_x"), QString("deny")); // exact names
  QCOMPARE(decide(""), QString("deny"));
  QCOMPARE(guardDecision({{"tool_name", "search_web"}}, allowed).value("decision").toString(),
           QString("allow"));
  QVERIFY(guardDecision({{"toolCall", QJsonObject{{"name", "run_command"}}}}, allowed)
              .value("reason").toString().contains("run_command"));
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
  QVERIFY(!args.contains("--allowedTools"));

  // The web, and only it, besides no tool.
  SessionOptions web = classic("/tmp");
  web.web = true;
  args = ClaudeConnection::arguments(web);
  QCOMPARE(args.at(args.indexOf("--tools") + 1), QString("WebSearch,WebFetch"));
  QCOMPARE(args.at(args.indexOf("--allowedTools") + 1), QString("WebSearch,WebFetch"));
  QCOMPARE(args.at(args.indexOf("--setting-sources") + 1), QString(""));

  SessionOptions agentic = classic("/tmp", "default");
  agentic.tools = true;
  agentic.mcpServers.append({"hodeia", "/opt/Hodeia", {"--mcp"}, {}});
  agentic.allowedTools = {"hodeia/search_library", "hodeia/get_page"};
  agentic.resumeSessionId = "s-42";
  args = ClaudeConnection::arguments(agentic);
  QVERIFY(!args.contains("--model")); // "default": the account's
  QVERIFY(!args.contains("--tools"));
  QCOMPARE(args.at(args.indexOf("--resume") + 1), QString("s-42"));
  QCOMPARE(args.at(args.indexOf("--allowedTools") + 1),
           QString("mcp__hodeia__search_library,mcp__hodeia__get_page"));
  QCOMPARE(args.at(args.size() - 2), QString("--mcp-config")); // takes several values: last
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

// Streaming: the text comes in pieces; the turn's text is the same.
void TestAgents::claudeStream() {
  std::unique_ptr<Connection> c(Registry::instance().create(profile("claude", FAKE_CLAUDE)));
  SessionOptions o = classic(m_dir->path());
  o.stream = true;
  QVERIFY(openSync(*c, o, 20000));
  QStringList pieces;
  connect(c.get(), &Connection::agentEvent, this, [&](const Event &e) {
    if (e.kind == Event::Kind::Text)
      pieces.append(e.text);
  });
  const TurnResult r = promptSync(*c, ask("Hello stream"), 20000);
  QCOMPARE(r.text, QString("Echo: Hello stream"));
  QVERIFY2(pieces.size() >= 3, qPrintable(pieces.join('|')));
  QCOMPARE(pieces.join(""), r.text); // no doubled text from the full message
  QVERIFY(loggedArgs(m_log).contains("--include-partial-messages"));
}

/* ── Codex ─────────────────────────────────────────────────────── */

void TestAgents::codexThreadParams() {
  SessionOptions o = classic("/tmp", "gpt-x");
  QJsonObject p = CodexConnection::threadParams(o, {});
  QCOMPARE(p.value("approvalPolicy").toString(), QString("never"));
  QCOMPARE(p.value("sandbox").toString(), QString("read-only"));
  QCOMPARE(p.value("baseInstructions").toString(), QString("SYSTEM PROMPT"));
  QCOMPARE(p.value("model").toString(), QString("gpt-x"));
  QJsonObject config = p.value("config").toObject();
  QCOMPARE(config.value("features").toObject().value("shell_tool"), QJsonValue(false));
  QCOMPARE(config.value("web_search").toString(), QString("disabled"));
  o.web = true;
  QCOMPARE(CodexConnection::threadParams(o, {}).value("config").toObject()
               .value("web_search").toString(), QString("live"));
  o.web = false;
  QVERIFY(!config.contains("mcp_servers"));

  // Agent mode: HodeIA's server, its allowed tools approved, the user's
  // servers disabled.
  o = classic("/tmp", "default");
  o.tools = true;
  o.mcpServers.append({"hodeia", "/opt/Hodeia", {"--mcp"}, {}});
  o.allowedTools = {"hodeia/search_library", "hodeia/get_page", "other/x"};
  p = CodexConnection::threadParams(o, {"mine"});
  QVERIFY(!p.contains("model")); // "default": Codex's own
  config = p.value("config").toObject();
  QVERIFY(!config.contains("features"));
  const QJsonObject servers = config.value("mcp_servers").toObject();
  QCOMPARE(servers.value("mine").toObject(), (QJsonObject{{"enabled", false}}));
  const QJsonObject hodeia = servers.value("hodeia").toObject();
  QCOMPARE(hodeia.value("command").toString(), QString("/opt/Hodeia"));
  QCOMPARE(hodeia.value("args").toArray(), QJsonArray{"--mcp"});
  const QJsonObject tools = hodeia.value("tools").toObject();
  QCOMPARE(tools.keys(), (QStringList{"get_page", "search_library"}));
  QCOMPARE(tools.value("get_page").toObject().value("approval_mode").toString(), QString("approve"));

  const QJsonArray input = CodexConnection::inputItems(
      {PromptPart::textPart("Hi"), PromptPart::image("image/png", "QUJD")});
  QCOMPARE(input.at(0).toObject(), (QJsonObject{{"type", "text"}, {"text", "Hi"}}));
  QCOMPARE(input.at(1).toObject().value("url").toString(), QString("data:image/png;base64,QUJD"));
}

void TestAgents::codexAnswer() {
  const auto r = run(profile("codex", FAKE_CODEX), classic(m_dir->path()), "Hello Codex");
  QVERIFY2(r, r ? "" : qPrintable(r.error()));
  QCOMPARE(r->stop, TurnResult::Stop::EndTurn);
  QCOMPARE(r->text, QString("Echo: Hello Codex"));
  QCOMPARE(loggedArgs(m_log), QStringList{"app-server"});
  // The user's MCP server (from config/read) is disabled for the thread.
  const QJsonObject start = find(loggedMessages(m_log), "thread/start");
  QCOMPARE(start.value("params").toObject().value("config").toObject()
               .value("mcp_servers").toObject().value("user-server").toObject(),
           (QJsonObject{{"enabled", false}}));
}

void TestAgents::codexStream() {
  std::unique_ptr<Connection> c(Registry::instance().create(profile("codex", FAKE_CODEX)));
  SessionOptions o = classic(m_dir->path());
  o.stream = true;
  QVERIFY(openSync(*c, o, 20000));
  QStringList pieces;
  connect(c.get(), &Connection::agentEvent, this, [&](const Event &e) {
    if (e.kind == Event::Kind::Text)
      pieces.append(e.text);
  });
  const TurnResult r = promptSync(*c, ask("Hello stream"), 20000);
  QCOMPARE(r.text, QString("Echo: Hello stream"));
  QVERIFY2(pieces.size() >= 3, qPrintable(pieces.join('|')));
  QCOMPARE(pieces.join(""), r.text); // no doubled text from the completed item
}

// Messages before and after a tool call: separated by a blank line, with
// or without streaming; the tool call is reported.
void TestAgents::codexTwoMessages() {
  for (const bool stream : {false, true}) {
    std::unique_ptr<Connection> c(Registry::instance().create(profile("codex", FAKE_CODEX)));
    SessionOptions o = classic(m_dir->path());
    o.stream = stream;
    QVERIFY(openSync(*c, o, 20000));
    QString text;
    QStringList tools;
    connect(c.get(), &Connection::agentEvent, this, [&](const Event &e) {
      if (e.kind == Event::Kind::Text)
        text += e.text;
      else if (e.kind == Event::Kind::ToolCall)
        tools.append(e.text + " " + e.status);
    });
    const TurnResult r = promptSync(*c, ask("TWO"), 20000);
    QCOMPARE(r.text, QString("First\n\nSecond"));
    QCOMPARE(text, r.text);
    QCOMPARE(tools, (QStringList{"hodeia/get_page in_progress", "hodeia/get_page completed"}));
  }
}

void TestAgents::codexError() {
  const auto r = run(profile("codex", FAKE_CODEX), classic(m_dir->path()), "ERROR");
  QVERIFY(r);
  QCOMPARE(r->stop, TurnResult::Stop::Error);
  QCOMPARE(r->error, QString("usage limit reached"));
}

void TestAgents::codexCancel() {
  std::unique_ptr<Connection> c(Registry::instance().create(profile("codex", FAKE_CODEX)));
  QVERIFY(openSync(*c, classic(m_dir->path()), 20000));
  QTimer::singleShot(300, c.get(), [&]() { c->cancel(); });
  const TurnResult r = promptSync(*c, ask("SLOW"), 20000);
  QCOMPARE(r.stop, TurnResult::Stop::Cancelled);
  QCOMPARE(r.text, QString("Partial"));
  const QJsonObject interrupt = find(loggedMessages(m_log), "turn/interrupt");
  QCOMPARE(interrupt.value("params").toObject().value("turnId").toString(), QString("turn-1"));
}

// A turn that ignores turn/interrupt: the process is killed.
void TestAgents::codexCancelKills() {
  std::unique_ptr<Connection> c(Registry::instance().create(profile("codex", FAKE_CODEX)));
  QVERIFY(openSync(*c, classic(m_dir->path()), 20000));
  QTimer::singleShot(300, c.get(), [&]() { c->cancel(); });
  QElapsedTimer t;
  t.start();
  const TurnResult r = promptSync(*c, ask("HANG"), 30000);
  QCOMPARE(r.stop, TurnResult::Stop::Cancelled);
  QVERIFY(t.elapsed() < 15000);
}

void TestAgents::codexRefusesApprovals() {
  const auto r = run(profile("codex", FAKE_CODEX), classic(m_dir->path()), "APPROVAL");
  QVERIFY(r);
  QCOMPARE(r->stop, TurnResult::Stop::EndTurn);
  for (const QJsonObject &m : loggedMessages(m_log))
    if (m.value("id").toInt() == 99 && !m.contains("method")) {
      QCOMPARE(m.value("result").toObject().value("decision").toString(), QString("decline"));
      return;
    }
  QFAIL("the approval request was not answered");
}

// A thread Codex knows is resumed; an unknown one gives a new thread.
void TestAgents::codexResume() {
  SessionOptions o = classic(m_dir->path());
  o.resumeSessionId = "thread-9";
  std::unique_ptr<Connection> c(Registry::instance().create(profile("codex", FAKE_CODEX)));
  QVERIFY(openSync(*c, o, 20000));
  QVERIFY(c->resumed());
  QCOMPARE(c->sessionId(), QString("thread-9"));
  c->close();

  o.resumeSessionId = "missing";
  c.reset(Registry::instance().create(profile("codex", FAKE_CODEX)));
  QVERIFY(openSync(*c, o, 20000));
  QVERIFY(!c->resumed());
  QCOMPARE(c->sessionId(), QString("thread-1"));
}

void TestAgents::codexListModels() {
  const auto models = Registry::instance().connector("codex")->listModels(profile("codex", FAKE_CODEX));
  QVERIFY2(models, models ? "" : qPrintable(models.error()));
  QCOMPARE(*models, (QStringList{"default", "m1", "m2"})); // both pages, no hidden one
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

// Backends saved without their agent (older version): found from the name.
void TestAgents::guessAgent() {
  QCOMPARE(AgentBackend::guessAgent("Claude Code"), QString("claude"));
  QCOMPARE(AgentBackend::guessAgent("Qwen"), QString("qwen"));
  QCOMPARE(AgentBackend::guessAgent("My Codex"), QString("codex"));
  QCOMPARE(AgentBackend::guessAgent("Remote"), QString());
  const auto p = AgentBackend::profileOf({{"name", "Claude Code"}, {"api", "agent"}});
  QVERIFY(p);
  QCOMPARE(p->id, QString("claude"));
}

QTEST_GUILESS_MAIN(TestAgents)
#include "tst_agents.moc"
