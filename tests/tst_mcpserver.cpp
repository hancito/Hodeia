// Unit tests for McpServer: the JSON-RPC protocol, both eras (legacy
// initialize handshake, modern per-request _meta), without any I/O.

#include "mcpserver.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

namespace {

QJsonObject send(McpServer &server, const QJsonObject &msg) {
  const QByteArray out =
      server.handleMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
  return QJsonDocument::fromJson(out).object();
}

QJsonObject request(int id, const QString &method,
                    const QJsonObject &params = {}) {
  QJsonObject msg{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}};
  if (!params.isEmpty())
    msg["params"] = params;
  return msg;
}

QJsonObject modernMeta(const QString &version = "2026-07-28") {
  return {{"_meta",
           QJsonObject{{"io.modelcontextprotocol/protocolVersion", version},
                       {"io.modelcontextprotocol/clientCapabilities",
                        QJsonObject{}}}}};
}

void addEcho(McpServer &server) {
  server.addTool({"echo", "Echo", "Echo the text back",
                  QJsonObject{{"type", "object"}},
                  [](const QJsonObject &args)
                      -> std::expected<QString, QString> {
                    if (!args.contains("text"))
                      return std::unexpected(QString("text is required"));
                    return args.value("text").toString();
                  }});
}

} // namespace

class TestMcpServer : public QObject {
  Q_OBJECT

private slots:
  void initializeKnownVersion();
  void initializeUnknownVersion();
  void notificationsGetNoAnswer();
  void legacyToolsList();
  void toolCall();
  void toolError();
  void unknownTool();
  void discover();
  void modernToolsList();
  void modernUnsupportedVersion();
  void unknownMethod();
  void parseError();
  void legacyPing();
};

void TestMcpServer::initializeKnownVersion() {
  McpServer server("1.0");
  const QJsonObject r = send(
      server, request(1, "initialize",
                      {{"protocolVersion", "2025-06-18"},
                       {"capabilities", QJsonObject{}},
                       {"clientInfo", QJsonObject{{"name", "t"},
                                                  {"version", "1"}}}}));
  QCOMPARE(r["id"].toInt(), 1);
  const QJsonObject result = r["result"].toObject();
  QCOMPARE(result["protocolVersion"].toString(), QString("2025-06-18"));
  QCOMPARE(result["serverInfo"].toObject()["name"].toString(),
           QString("hodeia"));
  QCOMPARE(result["serverInfo"].toObject()["version"].toString(),
           QString("1.0"));
  QVERIFY(result["capabilities"].toObject().contains("tools"));
  QCOMPARE(server.legacyVersion(), QString("2025-06-18"));
}

// A version we don't know: answer with our latest legacy one.
void TestMcpServer::initializeUnknownVersion() {
  McpServer server("1.0");
  const QJsonObject r = send(
      server, request(1, "initialize", {{"protocolVersion", "1999-01-01"}}));
  QCOMPARE(r["result"].toObject()["protocolVersion"].toString(),
           McpServer::legacyVersions().first());
}

void TestMcpServer::notificationsGetNoAnswer() {
  McpServer server("1.0");
  QVERIFY(server
              .handleMessage(R"({"jsonrpc":"2.0","method":"notifications/initialized"})")
              .isEmpty());
  // A client response (no method) is not answered either.
  QVERIFY(server.handleMessage(R"({"jsonrpc":"2.0","id":5,"result":{}})")
              .isEmpty());
}

void TestMcpServer::legacyToolsList() {
  McpServer server("1.0");
  addEcho(server);
  const QJsonObject result = send(server, request(2, "tools/list"))["result"]
                                 .toObject();
  const QJsonArray tools = result["tools"].toArray();
  QCOMPARE(tools.size(), 1);
  QCOMPARE(tools[0].toObject()["name"].toString(), QString("echo"));
  QVERIFY(tools[0].toObject().contains("inputSchema"));
  // Legacy results carry none of the modern fields.
  QVERIFY(!result.contains("resultType"));
  QVERIFY(!result.contains("ttlMs"));
}

void TestMcpServer::toolCall() {
  McpServer server("1.0");
  addEcho(server);
  const QJsonObject result =
      send(server, request(3, "tools/call",
                           {{"name", "echo"},
                            {"arguments", QJsonObject{{"text", "hi"}}}}))
          ["result"]
              .toObject();
  QCOMPARE(result["isError"].toBool(), false);
  const QJsonObject content = result["content"].toArray()[0].toObject();
  QCOMPARE(content["type"].toString(), QString("text"));
  QCOMPARE(content["text"].toString(), QString("hi"));
}

// A failing tool is a result with isError, for the model to read.
void TestMcpServer::toolError() {
  McpServer server("1.0");
  addEcho(server);
  const QJsonObject result =
      send(server, request(3, "tools/call", {{"name", "echo"}}))["result"]
          .toObject();
  QCOMPARE(result["isError"].toBool(), true);
  QCOMPARE(result["content"].toArray()[0].toObject()["text"].toString(),
           QString("text is required"));
}

void TestMcpServer::unknownTool() {
  McpServer server("1.0");
  const QJsonObject r =
      send(server, request(3, "tools/call", {{"name", "nope"}}));
  QCOMPARE(r["error"].toObject()["code"].toInt(), -32602);
}

void TestMcpServer::discover() {
  McpServer server("1.0");
  const QJsonObject result =
      send(server, request(1, "server/discover", modernMeta()))["result"]
          .toObject();
  QCOMPARE(result["resultType"].toString(), QString("complete"));
  const QJsonArray versions = result["supportedVersions"].toArray();
  QVERIFY(versions.contains(QJsonValue("2026-07-28")));
  QVERIFY(versions.contains(QJsonValue("2025-11-25")));
  QVERIFY(result.contains("ttlMs"));
  QCOMPARE(result["cacheScope"].toString(), QString("public"));
  QCOMPARE(result["_meta"]
               .toObject()["io.modelcontextprotocol/serverInfo"]
               .toObject()["name"]
               .toString(),
           QString("hodeia"));
}

void TestMcpServer::modernToolsList() {
  McpServer server("1.0");
  addEcho(server);
  const QJsonObject result =
      send(server, request(2, "tools/list", modernMeta()))["result"]
          .toObject();
  QCOMPARE(result["resultType"].toString(), QString("complete"));
  QVERIFY(result.contains("ttlMs"));
  QVERIFY(result.contains("cacheScope"));
  QCOMPARE(result["tools"].toArray().size(), 1);
  QVERIFY(server.legacyVersion().isEmpty()); // no handshake happened
}

void TestMcpServer::modernUnsupportedVersion() {
  McpServer server("1.0");
  const QJsonObject error =
      send(server, request(2, "tools/list", modernMeta("2099-01-01")))["error"]
          .toObject();
  QCOMPARE(error["code"].toInt(), -32022);
  QCOMPARE(error["data"].toObject()["requested"].toString(),
           QString("2099-01-01"));
  QVERIFY(error["data"].toObject()["supported"].toArray().contains(
      QJsonValue("2026-07-28")));
}

void TestMcpServer::unknownMethod() {
  McpServer server("1.0");
  QCOMPARE(send(server, request(9, "resources/list"))["error"]
               .toObject()["code"]
               .toInt(),
           -32601);
}

void TestMcpServer::parseError() {
  McpServer server("1.0");
  const QJsonObject r =
      QJsonDocument::fromJson(server.handleMessage("{not json")).object();
  QCOMPARE(r["error"].toObject()["code"].toInt(), -32700);
  QVERIFY(r["id"].isNull());
}

void TestMcpServer::legacyPing() {
  McpServer server("1.0");
  const QJsonObject r = send(server, request(4, "ping"));
  QVERIFY(r.contains("result"));
  QVERIFY(r["result"].toObject().isEmpty());
}

QTEST_GUILESS_MAIN(TestMcpServer)
#include "tst_mcpserver.moc"
