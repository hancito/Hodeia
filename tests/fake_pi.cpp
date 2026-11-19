// A fake `pi --mode rpc` for the tests: JSON commands in, responses and
// session events out, one object per line.  What a prompt does depends on
// its text:
//   "SLOW"  sends no answer until an "abort" command, then ends aborted;
//   "ERROR" ends with a provider error;
//   otherwise "Echo: <text>".
// FAKE_AGENT_LOG=<file>: the arguments (first line), then each command.

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

void respond(const QJsonObject &cmd, const QJsonObject &data = {}) {
  QJsonObject r{{"type", "response"},
                {"command", cmd.value("type")},
                {"success", true},
                {"data", data}};
  if (cmd.contains("id"))
    r["id"] = cmd.value("id");
  send(r);
}

void assistantEnd(const QString &text, const QString &stop, const QString &error = {}) {
  QJsonObject message{{"role", "assistant"},
                      {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", text}}}},
                      {"stopReason", stop}};
  if (!error.isEmpty())
    message["errorMessage"] = error;
  send({{"type", "message_end"}, {"message", message}});
}

bool readCommand(QJsonObject &cmd) {
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty())
      continue;
    const QByteArray bytes = QByteArray::fromStdString(line);
    log(bytes);
    cmd = QJsonDocument::fromJson(bytes).object();
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

  QJsonObject cmd;
  while (readCommand(cmd)) {
    const QString type = cmd.value("type").toString();
    if (type == "get_available_models") {
      respond(cmd, {{"models", QJsonArray{QJsonObject{{"provider", "P"}, {"id", "m1"}},
                                          QJsonObject{{"provider", "P"}, {"id", "m2"}}}}});
    } else if (type == "get_state") {
      respond(cmd, {{"sessionId", "fake-pi"}});
    } else if (type == "prompt") {
      const QString text = cmd.value("message").toString();
      respond(cmd, {{"disposition", "started"}});
      send({{"type", "agent_start"}});
      if (text.contains("SLOW")) {
        QJsonObject next;
        while (readCommand(next))
          if (next.value("type").toString() == "abort") {
            respond(next);
            break;
          }
        assistantEnd("Partial", "aborted", "Request was aborted");
        send({{"type", "agent_settled"}, {"aborted", true}});
      } else if (text.contains("ERROR")) {
        assistantEnd("", "error", "provider is down");
        send({{"type", "agent_settled"}, {"aborted", false}});
      } else {
        assistantEnd("Echo: " + text.left(200), "stop");
        send({{"type", "agent_settled"}, {"aborted", false}});
      }
    } else if (cmd.contains("id")) {
      respond(cmd);
    }
  }
  return 0;
}
