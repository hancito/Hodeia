// Unit tests for AnthropicApi: request building and response parsing for the
// Anthropic Messages API (no network).

#include "anthropicapi.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QtTest>

namespace {

PageImage grayImage(int width, int height) {
  PageImage img;
  img.width = width;
  img.height = height;
  img.data.assign(size_t(width) * height * 3, 128);
  return img;
}

QByteArray json(const char *text) { return QByteArray(text); }

} // namespace

class TestAnthropicApi : public QObject {
  Q_OBJECT

private slots:
  void endpointUrl_data();
  void endpointUrl();
  void headers();
  void bodyTextOnly();
  void bodyPageBlocks();
  void bodyFallbacks();
  void parseText();
  void parseSkipsNonTextBlocks();
  void parseApiError();
  void parseRefusal();
  void parseHttpErrorWithoutJson();
  void parseEmpty();
};

void TestAnthropicApi::endpointUrl_data() {
  QTest::addColumn<QString>("base");
  QTest::addColumn<QString>("expected");
  QTest::newRow("bare host") << "https://api.anthropic.com"
                             << "https://api.anthropic.com/v1/messages";
  QTest::newRow("trailing slash") << "https://api.anthropic.com/"
                                  << "https://api.anthropic.com/v1/messages";
  QTest::newRow("with /v1") << "https://api.anthropic.com/v1"
                            << "https://api.anthropic.com/v1/messages";
  QTest::newRow("with /v1/") << "https://api.anthropic.com/v1/"
                             << "https://api.anthropic.com/v1/messages";
  QTest::newRow("spaces") << "  https://api.anthropic.com/v1  "
                          << "https://api.anthropic.com/v1/messages";
  QTest::newRow("proxy path") << "https://proxy.example/anthropic"
                              << "https://proxy.example/anthropic/v1/messages";
}

void TestAnthropicApi::endpointUrl() {
  QFETCH(QString, base);
  QFETCH(QString, expected);
  QCOMPARE(AnthropicApi::endpointUrl(base, "messages").toString(), expected);
}

void TestAnthropicApi::headers() {
  QNetworkRequest request;
  AnthropicApi::applyHeaders(request, "sk-test", "claude-haiku-5-5");
  QCOMPARE(request.rawHeader("x-api-key"), QByteArray("sk-test"));
  QCOMPARE(request.rawHeader("anthropic-version"),
           QByteArray(AnthropicApi::kApiVersion));
  QCOMPARE(request.header(QNetworkRequest::ContentTypeHeader).toString(),
           QString("application/json"));
  QVERIFY(!request.hasRawHeader("anthropic-beta"));
}

void TestAnthropicApi::bodyTextOnly() {
  PromptPayload payload;
  payload.systemPrompt = "SYSTEM";
  payload.userMessage = "QUESTION";
  PageBlock page;
  page.pageNumber = 1;
  page.text = "CONTEXT";
  payload.pageBlocks.push_back(page);

  const QJsonObject body =
      AnthropicApi::buildBody(payload, "claude-haiku-5-5");
  QCOMPARE(body["model"].toString(), QString("claude-haiku-5-5"));
  QCOMPARE(body["max_tokens"].toInt(), AnthropicApi::kMaxTokens);
  QCOMPARE(body["system"].toString(), QString("SYSTEM"));
  QVERIFY(!body.contains("fallbacks"));

  const QJsonArray messages = body["messages"].toArray();
  QCOMPARE(messages.size(), 1);
  const QJsonObject user = messages[0].toObject();
  QCOMPARE(user["role"].toString(), QString("user"));
  // The instructions, then the page: text only.
  const QJsonArray content = user["content"].toArray();
  QCOMPARE(content.size(), 2);
  QCOMPARE(content[0].toObject()["type"].toString(), QString("text"));
  QCOMPARE(content[0].toObject()["text"].toString(),
           QString("QUESTION\n\n## DOCUMENT\n\n"));
  QCOMPARE(content[1].toObject()["type"].toString(), QString("text"));
  QCOMPARE(content[1].toObject()["text"].toString(),
           QString("## Page 1\nCONTEXT\n\n"));
}

void TestAnthropicApi::bodyPageBlocks() {
  PromptPayload payload;
  payload.userMessage = "SUMMARIZE";
  payload.pageBlocks.push_back({1, "first page", grayImage(16, 16)});
  payload.pageBlocks.push_back({2, "second page", PageImage{0, 0, {}}});

  const QJsonObject body =
      AnthropicApi::buildBody(payload, "claude-haiku-5-5");
  // No system prompt: no "system" key at all.
  QVERIFY(!body.contains("system"));

  const QJsonArray content =
      body["messages"].toArray()[0].toObject()["content"].toArray();
  // Instructions, page 1 image, page 1 text, page 2 text (no image).
  QCOMPARE(content.size(), 4);
  QVERIFY(content[0].toObject()["text"].toString().startsWith("SUMMARIZE"));

  const QJsonObject image = content[1].toObject();
  QCOMPARE(image["type"].toString(), QString("image"));
  const QJsonObject source = image["source"].toObject();
  QCOMPARE(source["type"].toString(), QString("base64"));
  QCOMPARE(source["media_type"].toString(), QString("image/jpeg"));
  const QByteArray jpeg =
      QByteArray::fromBase64(source["data"].toString().toLatin1());
  QVERIFY(jpeg.startsWith("\xFF\xD8")); // JPEG SOI marker

  QCOMPARE(content[2].toObject()["text"].toString(),
           QString("## Page 1\nfirst page\n\n"));
  QCOMPARE(content[3].toObject()["text"].toString(),
           QString("## Page 2\nsecond page\n\n"));
}

void TestAnthropicApi::bodyFallbacks() {
  PromptPayload payload;
  payload.userMessage = "Q";
  QVERIFY(AnthropicApi::withFallbacks("claude-opus-5-5"));
  const QJsonObject body = AnthropicApi::buildBody(payload, "claude-opus-5-5");
  QCOMPARE(body["fallbacks"].toString(), QString("default"));

  QNetworkRequest request;
  AnthropicApi::applyHeaders(request, "k", "claude-opus-5-5");
  QVERIFY(request.hasRawHeader("anthropic-beta"));
}

void TestAnthropicApi::parseText() {
  const auto r = AnthropicApi::parseResponse(
      json(R"({"content":[{"type":"text","text":"Hello "},)"
           R"({"type":"text","text":"world"}],"stop_reason":"end_turn"})"),
      200);
  QVERIFY(r.has_value());
  QCOMPARE(*r, QString("Hello world"));
}

void TestAnthropicApi::parseSkipsNonTextBlocks() {
  const auto r = AnthropicApi::parseResponse(
      json(R"({"content":[{"type":"thinking","thinking":"hmm"},)"
           R"({"type":"text","text":"answer"}],"stop_reason":"end_turn"})"),
      200);
  QVERIFY(r.has_value());
  QCOMPARE(*r, QString("answer"));
}

void TestAnthropicApi::parseApiError() {
  const auto r = AnthropicApi::parseResponse(
      json(R"({"type":"error","error":{"type":"authentication_error",)"
           R"("message":"invalid x-api-key"}})"),
      401);
  QVERIFY(!r.has_value());
  QCOMPARE(r.error(), QString("invalid x-api-key (authentication_error)"));
}

void TestAnthropicApi::parseRefusal() {
  const auto r = AnthropicApi::parseResponse(
      json(R"({"content":[],"stop_reason":"refusal",)"
           R"("stop_details":{"category":"cyber"}})"),
      200);
  QVERIFY(!r.has_value());
  QVERIFY(r.error().contains("cyber"));
}

void TestAnthropicApi::parseHttpErrorWithoutJson() {
  const auto r =
      AnthropicApi::parseResponse(json("<html>Bad Gateway</html>"), 502);
  QVERIFY(!r.has_value());
  QVERIFY(r.error().contains("502"));
}

void TestAnthropicApi::parseEmpty() {
  const auto r = AnthropicApi::parseResponse(
      json(R"({"content":[],"stop_reason":"end_turn"})"), 200);
  QVERIFY(!r.has_value());
}

QTEST_GUILESS_MAIN(TestAnthropicApi)
#include "tst_anthropicapi.moc"
