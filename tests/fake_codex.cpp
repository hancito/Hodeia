// A fake `codex app-server` for the tests: JSON-RPC in and out, one JSON
// object per line.  It has one user MCP server ("user-server", in
// config/read) and lists the models "m1", "secret" (hidden) and, on a
// second page, "m2".  thread/resume of "missing" fails.  What a turn does
// depends on the prompt text:
//   "ERROR"    an error notification, then a failed turn ("usage limit");
//   "SLOW"     writes "Partial", then waits for turn/interrupt;
//   "HANG"     writes "Partial" and ignores turn/interrupt;
//   "TWO"      two agent messages, "First" and "Second";
//   "APPROVAL" asks for a command approval first (the answer is logged);
//   otherwise "Echo: <text>", as three deltas then the completed item.
// FAKE_AGENT_LOG=<file>: the arguments (first line, JSON array), then each
// message received, are appended to that file.

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <cstdio>
#include <iostream>
#include <string>

namespace {

void log(const QByteArray &line) {
  const QByteArray path = qgetenv("FAKE_AGENT_LOG");
  if (path.isEmpty())
    return;
  QFile f(QString::fromLocal8Bit(path));
  if (f.open(QIODevice::Append))
    f.write(line + '\n');
}

void send(const QJsonObject &msg) {
  const QByteArray line = QJsonDocument(msg).toJson(QJsonDocument::Compact);
  std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
  std::fputc('\n', stdout);
  std::fflush(stdout);
}

void reply(const QJsonValue &id, const QJsonObject &result) {
  send({{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
}

void notify(const QString &method, QJsonObject params) {
  params["threadId"] = "thread-1";
  params["turnId"] = "turn-1";
  send({{"jsonrpc", "2.0"}, {"method", method}, {"params", params}});
}

bool readMessage(QJsonObject &msg) {
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty())
      continue;
    const QByteArray bytes = QByteArray::fromStdString(line);
    log(bytes);
    msg = QJsonDocument::fromJson(bytes).object();
    return true;
  }
  return false;
}

QJsonObject agentMessage(const QString &id, const QString &text) {
  return {{"type", "agentMessage"}, {"id", id}, {"text", text}};
}

/// One agent message: started, three deltas, completed.
void message(const QString &id, const QString &text) {
  notify("item/started", {{"item", agentMessage(id, "")}});
  const qsizetype third = qMax<qsizetype>(1, text.size() / 3);
  for (qsizetype i = 0; i < text.size(); i += third)
    notify("item/agentMessage/delta", {{"itemId", id}, {"delta", text.mid(i, third)}});
  notify("item/completed", {{"item", agentMessage(id, text)}});
}

void completed(const QString &status, const QJsonObject &error = {}) {
  QJsonObject turn{{"id", "turn-1"}, {"status", status}, {"items", QJsonArray{}}};
  if (!error.isEmpty())
    turn["error"] = error;
  notify("turn/completed", {{"turn", turn}});
}

/// Wait for turn/interrupt (answered); false at the end of input.
bool waitInterrupt(bool answer) {
  QJsonObject next;
  while (readMessage(next))
    if (next.value("method").toString() == "turn/interrupt") {
      if (answer)
        reply(next.value("id"), {});
      return true;
    }
  return false;
}

} // namespace

int main(int argc, char **argv) {
  QStringList args;
  for (int i = 1; i < argc; ++i)
    args << QString::fromLocal8Bit(argv[i]);
  log(QJsonDocument(QJsonArray::fromStringList(args)).toJson(QJsonDocument::Compact));

  QJsonObject msg;
  while (readMessage(msg)) {
    const QString method = msg.value("method").toString();
    const QJsonValue id = msg.value("id");
    const QJsonObject params = msg.value("params").toObject();
    if (method == "initialize") {
      reply(id, {{"userAgent", "fake-codex"}});
    } else if (method == "config/read") {
      reply(id, {{"config", QJsonObject{{"mcp_servers",
                                         QJsonObject{{"user-server",
                                                      QJsonObject{{"command", "x"}}}}}}}});
    } else if (method == "model/list") {
      if (params.value("cursor").toString() == "page2")
        reply(id, {{"data", QJsonArray{QJsonObject{{"id", "m2"}, {"model", "m2"}, {"hidden", false}}}},
                   {"nextCursor", QJsonValue::Null}});
      else
        reply(id, {{"data", QJsonArray{QJsonObject{{"id", "m1"}, {"model", "m1"}, {"hidden", false}},
                                       QJsonObject{{"id", "secret"}, {"model", "secret"}, {"hidden", true}}}},
                   {"nextCursor", "page2"}});
    } else if (method == "thread/start") {
      reply(id, {{"thread", QJsonObject{{"id", "thread-1"}}}});
    } else if (method == "thread/resume") {
      const QString thread = params.value("threadId").toString();
      if (thread == "missing")
        send({{"jsonrpc", "2.0"}, {"id", id},
              {"error", QJsonObject{{"code", -32600}, {"message", "no rollout found"}}}});
      else
        reply(id, {{"thread", QJsonObject{{"id", thread}}}});
    } else if (method == "turn/start") {
      QString text;
      for (const QJsonValue &v : params.value("input").toArray())
        text += v.toObject().value("text").toString();
      reply(id, {{"turn", QJsonObject{{"id", "turn-1"}, {"status", "inProgress"}}}});
      notify("turn/started", {{"turn", QJsonObject{{"id", "turn-1"}}}});
      if (text.contains("ERROR")) {
        notify("error", {{"error", QJsonObject{{"message", "usage limit reached"}}},
                         {"willRetry", false}});
        completed("failed", {{"message", "usage limit reached"}});
      } else if (text.contains("SLOW") || text.contains("HANG")) {
        message("i1", "Partial");
        if (waitInterrupt(text.contains("SLOW")) && text.contains("SLOW"))
          completed("interrupted");
        else
          while (readMessage(msg)) {} // HANG: ignores everything
      } else if (text.contains("TWO")) {
        message("i1", "First");
        notify("item/started", {{"item", QJsonObject{{"type", "mcpToolCall"}, {"id", "t1"},
                                                     {"server", "hodeia"}, {"tool", "get_page"}}}});
        notify("item/completed", {{"item", QJsonObject{{"type", "mcpToolCall"}, {"id", "t1"},
                                                       {"server", "hodeia"}, {"tool", "get_page"},
                                                       {"status", "completed"}}}});
        message("i2", "Second");
        completed("completed");
      } else {
        if (text.contains("APPROVAL")) {
          send({{"jsonrpc", "2.0"}, {"id", 99},
                {"method", "item/commandExecution/requestApproval"},
                {"params", QJsonObject{{"threadId", "thread-1"}, {"command", "rm -rf /"}}}});
          QJsonObject answer;
          readMessage(answer); // logged
        }
        message("i1", "Echo: " + text.left(400));
        completed("completed");
      }
    }
    // initialized, turn/interrupt outside a turn...: nothing to answer
  }
  return 0;
}
