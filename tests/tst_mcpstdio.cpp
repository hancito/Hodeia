// End-to-end test of `Hodeia --mcp`: the real executable started over pipes
// the way an agent starts it, legacy and modern clients.  On Windows this is
// what checks that the GUI-subsystem executable talks over stdin/stdout.

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QtTest>

namespace {

/// Send `requests` (one per line), close stdin, return the response lines.
QList<QJsonObject> exchange(const QList<QJsonObject> &requests) {
  QProcess hodeia;
  hodeia.setProgram(QStringLiteral(HODEIA_EXE));
  hodeia.setArguments({"--mcp"});
  hodeia.start();
  if (!hodeia.waitForStarted(30000))
    return {};
  for (const QJsonObject &r : requests)
    hodeia.write(QJsonDocument(r).toJson(QJsonDocument::Compact) + '\n');
  hodeia.closeWriteChannel();
  if (!hodeia.waitForFinished(60000)) {
    hodeia.kill();
    return {};
  }
  QList<QJsonObject> out;
  for (const QByteArray &line : hodeia.readAllStandardOutput().split('\n')) {
    if (line.trimmed().isEmpty())
      continue;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    // Anything on stdout that is not a JSON-RPC message breaks the client.
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
      qWarning() << "non-JSON line on stdout:" << line;
      return {};
    }
    out.append(doc.object());
  }
  return out;
}

QJsonObject request(int id, const QString &method,
                    const QJsonObject &params = {}) {
  QJsonObject msg{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}};
  if (!params.isEmpty())
    msg["params"] = params;
  return msg;
}

} // namespace

class TestMcpStdio : public QObject {
  Q_OBJECT

private slots:
  void legacyClient();
  void modernClient();
};

void TestMcpStdio::legacyClient() {
  const auto replies = exchange(
      {request(1, "initialize", {{"protocolVersion", "2025-11-25"},
                                 {"capabilities", QJsonObject{}}}),
       QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}},
       request(2, "tools/list"),
       request(3, "tools/call",
               {{"name", "ping"}, {"arguments", QJsonObject{}}})});
  QCOMPARE(replies.size(), 3);
  QCOMPARE(replies[0]["id"].toInt(), 1);
  QCOMPARE(replies[0]["result"].toObject()["protocolVersion"].toString(),
           QString("2025-11-25"));
  QCOMPARE(replies[1]["result"]
               .toObject()["tools"]
               .toArray()[0]
               .toObject()["name"]
               .toString(),
           QString("ping"));
  QVERIFY(replies[2]["result"]
              .toObject()["content"]
              .toArray()[0]
              .toObject()["text"]
              .toString()
              .startsWith("pong"));
}

void TestMcpStdio::modernClient() {
  const QJsonObject meta{
      {"_meta",
       QJsonObject{{"io.modelcontextprotocol/protocolVersion", "2026-07-28"},
                   {"io.modelcontextprotocol/clientCapabilities",
                    QJsonObject{}}}}};
  QJsonObject call = meta;
  call["name"] = "ping";
  const auto replies = exchange({request(1, "server/discover", meta),
                                 request(2, "tools/call", call)});
  QCOMPARE(replies.size(), 2);
  QCOMPARE(replies[0]["result"].toObject()["resultType"].toString(),
           QString("complete"));
  QCOMPARE(replies[1]["result"].toObject()["resultType"].toString(),
           QString("complete"));
  QCOMPARE(replies[1]["result"].toObject()["isError"].toBool(), false);
}

QTEST_GUILESS_MAIN(TestMcpStdio)
#include "tst_mcpstdio.moc"
