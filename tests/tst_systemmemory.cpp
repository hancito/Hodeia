// Unit tests for SystemMemory: the available memory and how many parallel
// tasks fit in it.

#include "systemmemory.h"

#include <QtTest>

class TestSystemMemory : public QObject {
  Q_OBJECT

private slots:
  void available();
  void tasksThatFit_data();
  void tasksThatFit();
};

// Known on the platforms HodeIA ships for, and plausible.
void TestSystemMemory::available() {
  const qint64 bytes = SystemMemory::availableBytes();
  QVERIFY2(bytes > (qint64(16) << 20), qPrintable(QString::number(bytes)));
  QVERIFY(bytes < (qint64(1) << 50));
}

void TestSystemMemory::tasksThatFit_data() {
  QTest::addColumn<qint64>("available");
  QTest::addColumn<int>("maxTasks");
  QTest::addColumn<int>("expected");
  constexpr qint64 MiB = qint64(1) << 20;
  // 256 MiB per task, half the memory.
  QTest::newRow("plenty, capped by cores") << 16384 * MiB << 6 << 6;
  QTest::newRow("memory bound") << 1536 * MiB << 6 << 3;
  QTest::newRow("rounds down") << 1535 * MiB << 6 << 2;
  QTest::newRow("too little: still one") << 100 * MiB << 6 << 1;
  QTest::newRow("unknown: fallback") << qint64(-1) << 6 << 2;
  QTest::newRow("unknown, fallback capped") << qint64(-1) << 1 << 1;
  QTest::newRow("no cores reported") << 16384 * MiB << 0 << 1;
}

void TestSystemMemory::tasksThatFit() {
  QFETCH(qint64, available);
  QFETCH(int, maxTasks);
  QFETCH(int, expected);
  QCOMPARE(SystemMemory::tasksThatFit(available, qint64(256) << 20, 0.5,
                                      maxTasks, /*fallback=*/2),
           expected);
}

QTEST_GUILESS_MAIN(TestSystemMemory)
#include "tst_systemmemory.moc"
