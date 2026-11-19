// A fake ACP agent for the tests: JSON-RPC over stdin/stdout, one message per
// line, protocol version 1.  What it does depends on the prompt text:
//   "PERMISSION" asks for a permission and answers with the chosen option;
//   "SLOW"       sends one chunk, then waits for session/cancel;
//   otherwise    answers "Echo: <prompt text>" in two chunks.
// FAKE_ACP_AUTH_FAIL=1 makes session/new fail as not logged in.
// FAKE_ACP_STYLE=models: like Qwen Code, the session lists `models` (chosen
// with session/set_model) and the system prompt cannot be replaced.
// FAKE_ACP_LOG=<file>: each received message is appended to that file.

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <iostream>
#include <string>

namespace {

void send(const QJsonObject &msg) {
  const QByteArray line = QJsonDocument(msg).toJson(QJsonDocument::Compact);
  std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
  std::fputc('\n', stdout);
  std::fflush(stdout);
}

void reply(const QJsonValue &id, const QJsonValue &result) {
  send({{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
}

void replyError(const QJsonValue &id, int code, const QString &message) {
  send({{"jsonrpc", "2.0"},
        {"id", id},
        {"error", QJsonObject{{"code", code}, {"message", message}}}});
}

void chunk(const QString &session, const QString &text) {
  send({{"jsonrpc", "2.0"},
        {"method", "session/update"},
        {"params",
         QJsonObject{{"sessionId", session},
                     {"update", QJsonObject{{"sessionUpdate", "agent_message_chunk"},
                                            {"content", QJsonObject{{"type", "text"},
                                                                    {"text", text}}}}}}}});
}

bool readMessage(QJsonObject &msg) {
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty())
      continue;
    const QByteArray bytes = QByteArray::fromStdString(line);
    const QByteArray logPath = qgetenv("FAKE_ACP_LOG");
    if (!logPath.isEmpty()) {
      QFile log(QString::fromLocal8Bit(logPath));
      if (log.open(QIODevice::Append))
        log.write(bytes + '\n');
    }
    msg = QJsonDocument::fromJson(bytes).object();
    return true;
  }
  return false;
}

} // namespace

int main() {
  const QString session = "fake-session";
  QJsonObject msg;
  while (readMessage(msg)) {
    const QString method = msg.value("method").toString();
    const QJsonValue id = msg.value("id");
    const QJsonObject params = msg.value("params").toObject();

    const bool modelsStyle = qgetenv("FAKE_ACP_STYLE") == "models";
    if (method == "initialize") {
      const QJsonObject caps =
          modelsStyle ? QJsonObject{}
                      : QJsonObject{{"sessionCapabilities",
                                     QJsonObject{{"systemPrompt",
                                                  QJsonObject{{"mode", QJsonObject{{"override", QJsonObject{}}}}}}}}};
      reply(id, QJsonObject{{"protocolVersion", 1},
                            {"agentCapabilities", caps},
                            {"authMethods", QJsonArray{}}});
    } else if (method == "session/new") {
      if (qgetenv("FAKE_ACP_AUTH_FAIL") == "1") {
        replyError(id, -32000, "Authentication required");
        continue;
      }
      if (modelsStyle) {
        reply(id, QJsonObject{
                      {"sessionId", session},
                      {"models", QJsonObject{{"currentModelId", "qwen-a"},
                                             {"availableModels",
                                              QJsonArray{QJsonObject{{"modelId", "qwen-a"}},
                                                         QJsonObject{{"modelId", "qwen-b"}}}}}}});
        continue;
      }
      reply(id, QJsonObject{
                    {"sessionId", session},
                    {"configOptions",
                     QJsonArray{QJsonObject{
                         {"id", "model"},
                         {"options", QJsonArray{QJsonObject{{"value", "default"}},
                                                QJsonObject{{"value", "sonnet"}}}}}}}});
    } else if (method == "session/set_model") {
      reply(id, QJsonObject{});
    } else if (method == "session/set_config_option") {
      if (params.value("value").toString() == "unknown-model")
        replyError(id, -32602, "Unknown model");
      else
        reply(id, QJsonObject{});
    } else if (method == "session/prompt") {
      QString text;
      for (const QJsonValue &b : params.value("prompt").toArray())
        if (b.toObject().value("type").toString() == "text")
          text += b.toObject().value("text").toString();

      if (text.contains("PERMISSION")) {
        send({{"jsonrpc", "2.0"},
              {"id", 900},
              {"method", "session/request_permission"},
              {"params",
               QJsonObject{{"sessionId", session},
                           {"toolCall", QJsonObject{{"toolCallId", "t1"},
                                                    {"title", "Read file"}}},
                           {"options",
                            QJsonArray{QJsonObject{{"optionId", "yes"},
                                                   {"name", "Allow"},
                                                   {"kind", "allow_once"}},
                                       QJsonObject{{"optionId", "no"},
                                                   {"name", "Reject"},
                                                   {"kind", "reject_once"}}}}}}});
        QJsonObject answer;
        if (!readMessage(answer))
          return 1;
        const QJsonObject outcome =
            answer.value("result").toObject().value("outcome").toObject();
        chunk(session, "Permission: " + outcome.value("outcome").toString() + " " +
                           outcome.value("optionId").toString());
        reply(id, QJsonObject{{"stopReason", "end_turn"}});
      } else if (text.contains("SLOW")) {
        chunk(session, "Partial");
        QJsonObject next;
        while (readMessage(next))
          if (next.value("method").toString() == "session/cancel")
            break;
        reply(id, QJsonObject{{"stopReason", "cancelled"}});
      } else {
        chunk(session, "Echo: ");
        chunk(session, text.left(200));
        reply(id, QJsonObject{{"stopReason", "end_turn"}});
      }
    } else if (!id.isUndefined()) {
      replyError(id, -32601, "Method not found");
    }
  }
  return 0;
}
