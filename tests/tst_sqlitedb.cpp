// Unit tests for SqliteStatement: every SQLite failure is reported as an
// error, not only SQLITE_ERROR.

#include "sqlitedb.h"

#include <QTemporaryDir>
#include <QtTest>

class TestSqliteDb : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();
  void stepRowsAndDone();
  void constraintIsAnError();
  void execReturningRow();

private:
  QTemporaryDir *m_dir = nullptr;
  SqliteDb m_db;
};

void TestSqliteDb::init() {
  m_dir = new QTemporaryDir;
  QVERIFY(m_db.open(m_dir->filePath("t.db")).has_value());
  QVERIFY(m_db.exec("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT NOT NULL "
                    "UNIQUE, n INTEGER CHECK (n >= 0))")
              .has_value());
}

void TestSqliteDb::cleanup() {
  m_db.close();
  delete m_dir;
  m_dir = nullptr;
}

void TestSqliteDb::stepRowsAndDone() {
  QVERIFY(m_db.exec("INSERT INTO t (name, n) VALUES ('a', 1), ('b', 2)").has_value());
  SqliteStatement stmt = m_db.prepare("SELECT name FROM t ORDER BY id");
  QVERIFY(stmt.isValid());
  auto r = stmt.step();
  QVERIFY(r && *r);
  QCOMPARE(stmt.columnText(0), QString("a"));
  r = stmt.step();
  QVERIFY(r && *r);
  r = stmt.step();
  QVERIFY(r && !*r);
}

void TestSqliteDb::constraintIsAnError() {
  SqliteStatement insert = m_db.prepare("INSERT INTO t (name, n) VALUES (?, ?)");
  (void)insert.bind(1, QString("a"));
  (void)insert.bind(2, 1);
  auto r = insert.exec();
  QVERIFY2(r.has_value(), r ? "" : qPrintable(r.error()));

  // UNIQUE
  insert.resetAndClearBindings();
  (void)insert.bind(1, QString("a"));
  (void)insert.bind(2, 1);
  r = insert.exec();
  QVERIFY(!r);
  QVERIFY2(r.error().contains("UNIQUE"), qPrintable(r.error()));

  // CHECK
  insert.resetAndClearBindings();
  (void)insert.bind(1, QString("b"));
  (void)insert.bind(2, -1);
  r = insert.exec();
  QVERIFY(!r);
  QVERIFY2(r.error().contains("CHECK"), qPrintable(r.error()));

  // NOT NULL, through step() (RETURNING)
  SqliteStatement returning =
      m_db.prepare("INSERT INTO t (name, n) VALUES (NULL, 1) RETURNING id");
  auto s = returning.step();
  QVERIFY(!s);
  QVERIFY2(s.error().contains("NOT NULL"), qPrintable(s.error()));
}

void TestSqliteDb::execReturningRow() {
  // exec() on a statement that yields a row: it ran, so it succeeds.
  SqliteStatement stmt =
      m_db.prepare("INSERT INTO t (name, n) VALUES ('c', 3) RETURNING id");
  auto r = stmt.exec();
  QVERIFY(r && *r);
}

QTEST_GUILESS_MAIN(TestSqliteDb)
#include "tst_sqlitedb.moc"
