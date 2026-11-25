// Unit tests for the backend-independent parts of chatbackend: the prompt
// budget of remote backends and the capabilities each backend reports.
// The local (llama.cpp) backend needs a model and is not covered here.

#include "chatbackend.h"
#include "remotebackend.h"

#include <QImage>
#include <QJsonObject>
#include <QtTest>

class TestChatBackend : public QObject {
  Q_OBJECT

private slots:
  void remoteBudget_data();
  void remoteBudget();
  void remoteCaps_data();
  void remoteCaps();
  void remoteImagePart();
};

void TestChatBackend::remoteBudget_data() {
  QTest::addColumn<int>("kind");
  QTest::newRow("openai") << int(ActiveBackend::Kind::OpenAi);
  QTest::newRow("anthropic") << int(ActiveBackend::Kind::Anthropic);
}

void TestChatBackend::remoteBudget() {
  QFETCH(int, kind);
  ActiveBackend active;
  active.kind = ActiveBackend::Kind(kind);
  // The requested size does not matter remotely: no memory fitting.
  for (int promptTokens : {0, 4096, 1'000'000}) {
    const ContextBudget b = estimateContextBudget(active, promptTokens, 4096);
    QVERIFY(b.remote);
    QCOMPARE(b.autoTokens, ContextBudget::RemoteAutoTokens);
    QCOMPARE(b.maxTokens, ContextBudget::RemoteMaxTokens);
    QCOMPARE(b.placement, ContextBudget::Placement::Unknown);
    QVERIFY(!b.truncated);
  }
}

void TestChatBackend::remoteCaps_data() {
  QTest::addColumn<int>("kind");
  QTest::newRow("openai") << int(ActiveBackend::Kind::OpenAi);
  QTest::newRow("anthropic") << int(ActiveBackend::Kind::Anthropic);
}

void TestChatBackend::remoteCaps() {
  QFETCH(int, kind);
  ActiveBackend active;
  active.kind = ActiveBackend::Kind(kind);
  active.modelName = "some-model";
  active.config = {{"url", "http://localhost:1"}, {"api_key", "k"}};
  const auto backend = makeChatBackend(active);
  QVERIFY(backend);
  QVERIFY(!backend->caps().usesLocalGpu);
  backend->cancel();  // nothing in flight: must be a no-op
  backend->release(); // no-op remotely
}

// A page whose rows are not 4-byte aligned (width * 3 = 15): the PNG sent
// must hold exactly its pixels (it once read past the buffer, shearing it).
void TestChatBackend::remoteImagePart() {
  constexpr int w = 5, h = 3;
  PageImage img;
  img.width = w;
  img.height = h;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      img.data.insert(img.data.end(),
                      {static_cast<unsigned char>(x * 40),
                       static_cast<unsigned char>(y * 80), 7});

  const QJsonObject part = RemoteBackend::imageToPart(img);
  QCOMPARE(part["type"].toString(), QString("image_url"));
  const QString url = part["image_url"].toObject()["url"].toString();
  const QString prefix = "data:image/png;base64,";
  QVERIFY(url.startsWith(prefix));
  const QImage png = QImage::fromData(
      QByteArray::fromBase64(url.mid(prefix.size()).toLatin1()), "PNG");
  QCOMPARE(png.size(), QSize(w, h));
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      QCOMPARE(png.pixelColor(x, y), QColor(x * 40, y * 80, 7));
}

QTEST_GUILESS_MAIN(TestChatBackend)
#include "tst_chatbackend.moc"
