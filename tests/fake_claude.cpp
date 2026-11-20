// A fake `claude` for the tests: stream-json in, stream-json out, one JSON
// object per line.  What it does depends on the user message text:
//   "LIMIT" answers like Claude Code at its usage limit (an error result);
//   "SLOW"  sends "Partial", then waits for an interrupt control request;
//   "HANG"  sends "Partial" and ignores the interrupt;
//   otherwise "Echo: <text>".
// FAKE_CLAUDE_SESSION=<id>: the session id it reports (default:
// the id given with --resume, else "fake-claude").
// FAKE_CLAUDE_RESUME_FAIL=1: with --resume, exits with an error at once,
// as Claude Code does for a session it does not have.
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

bool g_stream = false; // --include-partial-messages

/// With --include-partial-messages, the text first as stream_event deltas
/// (three pieces), as Claude Code sends it.
void deltas(const QString &text) {
  if (!g_stream)
    return;
  send({{"type", "stream_event"},
        {"event", QJsonObject{{"type", "content_block_start"},
                              {"content_block", QJsonObject{{"type", "text"}, {"text", ""}}}}}});
  const qsizetype third = qMax<qsizetype>(1, text.size() / 3);
  for (qsizetype i = 0; i < text.size(); i += third)
    send({{"type", "stream_event"},
          {"event", QJsonObject{{"type", "content_block_delta"},
                                {"delta", QJsonObject{{"type", "text_delta"},
                                                      {"text", text.mid(i, third)}}}}}});
}

void assistant(const QString &text) {
  deltas(text);
  send({{"type", "assistant"},
        {"message", QJsonObject{{"role", "assistant"},
                                {"content", QJsonArray{QJsonObject{{"type", "text"},
                                                                   {"text", text}}}}}}});
}

void result(const QString &text, bool isError) {
  send({{"type", "result"},
        {"subtype", "success"},
        {"is_error", isError},
        {"result", text},
        {"stop_reason", "end_turn"},
        {"session_id", "fake-claude"}});
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

} // namespace

int main(int argc, char **argv) {
  QStringList args;
  for (int i = 1; i < argc; ++i)
    args << QString::fromLocal8Bit(argv[i]);
  log(QJsonDocument(QJsonArray::fromStringList(args)).toJson(QJsonDocument::Compact));
  g_stream = args.contains("--include-partial-messages");

  if (qgetenv("FAKE_CLAUDE_RESUME_FAIL") == "1" && args.contains("--resume")) {
    std::fputs("No conversation found with session ID\n", stderr);
    return 1;
  }
  QString sessionId = QString::fromLocal8Bit(qgetenv("FAKE_CLAUDE_SESSION"));
  if (sessionId.isEmpty()) {
    const qsizetype resume = args.indexOf("--resume");
    sessionId = resume >= 0 && resume + 1 < args.size() ? args[resume + 1]
                                                         : QStringLiteral("fake-claude");
  }

  QJsonObject msg;
  while (readMessage(msg)) {
    if (msg.value("type").toString() != "user")
      continue;
    QString text;
    for (const QJsonValue &b : msg.value("message").toObject().value("content").toArray())
      if (b.toObject().value("type").toString() == "text")
        text += b.toObject().value("text").toString();
    send({{"type", "system"}, {"subtype", "init"}, {"session_id", sessionId}});

    if (text.contains("LIMIT")) {
      send({{"type", "rate_limit_event"},
            {"rate_limit_info", QJsonObject{{"status", "rejected"}}}});
      assistant("You've hit your limit");
      result("You've hit your limit", true);
    } else if (text.contains("SLOW") || text.contains("HANG")) {
      assistant("Partial");
      QJsonObject next;
      while (readMessage(next)) {
        if (next.value("type").toString() != "control_request")
          continue;
        send({{"type", "control_response"},
              {"response", QJsonObject{{"subtype", "success"},
                                       {"request_id", next.value("request_id")}}}});
        if (text.contains("SLOW")) {
          result("", false);
          break;
        }
      }
    } else {
      assistant("Echo: " + text.left(400));
      result("Echo: " + text.left(400), false);
    }
  }
  return 0;
}
