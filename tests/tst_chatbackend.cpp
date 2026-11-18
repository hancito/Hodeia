// Unit tests for the backend-independent parts of chatbackend: the prompt
// budget of remote backends and the capabilities each backend reports.
// The local (llama.cpp) backend needs a model and is not covered here.

#include "chatbackend.h"

#include <QtTest>

class TestChatBackend : public QObject {
  Q_OBJECT

private slots:
  void remoteBudget_data();
  void remoteBudget();
  void remoteCaps_data();
  void remoteCaps();
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

QTEST_GUILESS_MAIN(TestChatBackend)
#include "tst_chatbackend.moc"
