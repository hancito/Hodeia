// Unit tests for Reindexer: embedding again the chunks of a project, batch
// by batch, resumable, as recorded by migration v3.

#include "projectstore.h"
#include "reindexer.h"
#include "sqlitedb.h"
#include "sqlitevec.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TestReindexer : public QObject {
  Q_OBJECT

private slots:
  void nothingToDo();
  void reembedInBatches();
};

// A project without the task: no status, a step does nothing.
void TestReindexer::nothingToDo() {
  QTemporaryDir dir;
  SqliteDb db;
  QVERIFY(ProjectStore::open(db, dir.filePath("P.db")).has_value());
  QVERIFY(!Reindexer::status(db));
  SqliteVecManager embeddings(nullptr); // never loaded: no chunk to embed
  const auto n = Reindexer::step(db, embeddings, 8);
  QVERIFY(n.has_value());
  QCOMPARE(*n, 0);
}

void TestReindexer::reembedInBatches() {
  const QString model = QStringLiteral(HODEIA_E5_MODEL);
  if (!QFile::exists(model))
    QSKIP("E5 model not available (Git LFS)");
  QTemporaryDir dir;
  SqliteDb db;
  QVERIFY(ProjectStore::open(db, dir.filePath("P.db")).has_value());

  // Three chunks with zero vectors, as if embedded the old way, and the task
  // that migration v3 records for them.
  const QStringList texts{
      "Absenteeism and peer effects: employees are absent from work more "
      "often when their co-workers are often absent.",
      "Monetary policy: the central bank raised its key interest rate to bring "
      "inflation back to its target.",
      "The iterated prisoner's dilemma: cooperation emerges when the game is "
      "repeated and players can punish defection."};
  SqliteStatement insert = db.prepare(
      "INSERT INTO chunks (embedding, paper_hash, page, chunk_idx, chunk_text, "
      "citation, clean_title, page_metadata) "
      "VALUES (vec_f32(?), ?, 1, 0, ?, '', '', '')");
  for (int i = 0; i < texts.size(); ++i) {
    insert.resetAndClearBindings();
    (void)insert.bind(1, QByteArray(384 * sizeof(float), '\0'));
    (void)insert.bind(2, QString("hash%1").arg(i));
    (void)insert.bind(3, texts[i]);
    QVERIFY(insert.exec().has_value());
  }
  QVERIFY(db.exec("INSERT INTO Maintenance (task) VALUES ('reembed')"));

  auto status = Reindexer::status(db);
  QVERIFY(status);
  QCOMPARE(status->done, 0);
  QCOMPARE(status->total, 3);

  SqliteVecManager embeddings(nullptr);
  QVERIFY(embeddings.initialize(model));
  auto n = Reindexer::step(db, embeddings, 2);
  QVERIFY2(n.has_value(), n ? "" : qPrintable(n.error()));
  QCOMPARE(*n, 2);
  status = Reindexer::status(db);
  QVERIFY(status);
  QCOMPARE(status->done, 2); // committed: a restart resumes here

  n = Reindexer::step(db, embeddings, 2);
  QVERIFY(n && *n == 1);
  n = Reindexer::step(db, embeddings, 2);
  QVERIFY(n && *n == 0); // over: the task is removed
  QVERIFY(!Reindexer::status(db));

  // The vectors are real now: each question finds its passage.
  const auto hits = embeddings.search(db, "Why do people cooperate in repeated games?", 3);
  QVERIFY(!hits.isEmpty());
  QCOMPARE(hits.first().paperHash, QString("hash2"));
  QCOMPARE(embeddings.search(db, "interest rates and inflation", 1).first().paperHash,
           QString("hash1"));
}

QTEST_GUILESS_MAIN(TestReindexer)
#include "tst_reindexer.moc"
