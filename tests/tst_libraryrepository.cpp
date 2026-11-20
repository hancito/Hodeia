// Unit tests for LibraryRepository, on a temporary project database created
// by ProjectStore (real schema, sqlite-vec chunks table).

#include "libraryrepository.h"
#include "projectstore.h"
#include "sqlitedb.h"

#include <QTemporaryDir>
#include <QtTest>

namespace {

PaperSummary summary(const QString &title) {
  PaperSummary s;
  s.title = title;
  s.authors = {"J. Walker", "M. Ben-Akiva"};
  s.journal = "Mathematical Social Sciences";
  s.year = 2002;
  s.topics = {"choice", "latent"};
  return s;
}

void addChunk(SqliteDb &db, const QString &hash) {
  // Same statement as SqliteVecManager::insertChunks().
  SqliteStatement stmt = db.prepare(
      "INSERT INTO chunks (embedding, paper_hash, page, chunk_idx, "
      "chunk_text, citation, clean_title, page_metadata) "
      "VALUES (vec_f32(?), ?, ?, ?, ?, ?, ?, ?)");
  QVERIFY(stmt.isValid());
  const QByteArray embedding(384 * int(sizeof(float)), '\0');
  (void)stmt.bind(1, embedding);
  (void)stmt.bind(2, hash);
  (void)stmt.bind(3, 0);
  (void)stmt.bind(4, 0);
  (void)stmt.bind(5, QString("text"));
  (void)stmt.bind(6, QString("file.pdf"));
  (void)stmt.bind(7, QString("file.pdf"));
  (void)stmt.bind(8, QString());
  auto r = stmt.exec();
  QVERIFY2(r.has_value(), qPrintable(r ? QString() : r.error()));
}

int count(SqliteDb &db, const QString &sql) {
  SqliteStatement stmt = db.prepare(sql);
  auto step = stmt.step();
  return step && *step ? stmt.columnInt(0) : -1;
}

} // namespace

class TestLibraryRepository : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();
  void saveSummary();
  void saveSummaryAgainReplaces();
  void summaryWithQuotes();
  void queries();
  void removePapers();
  void removeQueries();
  void indexedPapers();
  void addPapers();
  void addPapersAllOrNone();

private:
  QTemporaryDir *m_dir = nullptr;
  SqliteDb m_db;
};

void TestLibraryRepository::init() {
  m_dir = new QTemporaryDir;
  QVERIFY(ProjectStore::open(m_db, m_dir->filePath("P.db")).has_value());
}

void TestLibraryRepository::cleanup() {
  m_db.close();
  delete m_dir;
  m_dir = nullptr;
}

void TestLibraryRepository::saveSummary() {
  addChunk(m_db, "h1");
  LibraryRepository repo(m_db);
  QVERIFY(repo.saveSummary("h1", summary("Hybrid Choice"), "{\"raw\":1}",
                           "model-x"));

  const QList<SummaryRow> rows = repo.summaries();
  QCOMPARE(rows.size(), 1);
  const SummaryRow &r = rows.first();
  QCOMPARE(r.paperHash, QString("h1"));
  QCOMPARE(r.title, QString("Hybrid Choice"));
  QCOMPARE(r.authors, QString("J. Walker; M. Ben-Akiva"));
  QCOMPARE(r.journal, QString("Mathematical Social Sciences"));
  QCOMPARE(r.year, QString("2002"));
  QCOMPARE(r.topics, QString("choice, latent"));
  QCOMPARE(r.llmodel, QString("model-x"));
  QCOMPARE(r.summaryJson.toByteArray(), QByteArray("{\"raw\":1}"));
  QVERIFY(r.date.isValid());
  QCOMPARE(r.citesCount, 0);
  QCOMPARE(repo.summaryTitle("h1"), QString("Hybrid Choice"));
  QVERIFY(repo.summaryTitle("unknown").isEmpty());

  // The chunks now carry the summary's title and "Authors (year)".
  SqliteStatement stmt = m_db.prepare(
      "SELECT clean_title, citation FROM chunks WHERE paper_hash = 'h1'");
  auto step = stmt.step();
  QVERIFY(step && *step);
  QCOMPARE(stmt.columnText(0), QString("Hybrid Choice"));
  QCOMPARE(stmt.columnText(1), QString("J. Walker, M. Ben-Akiva (2002)"));

  // Registered for an OpenAlex lookup, listed with the summarized papers.
  const QList<CitationRow> cits = repo.citations();
  QCOMPARE(cits.size(), 1);
  QCOMPARE(cits.first().paperHash, QString("h1"));
  QCOMPARE(count(m_db, "SELECT COUNT(*) FROM Citations WHERE "
                       "lookup_status = 'pending'"),
           1);
}

void TestLibraryRepository::saveSummaryAgainReplaces() {
  LibraryRepository repo(m_db);
  QVERIFY(repo.saveSummary("h1", summary("First"), "{}", "m"));
  QVERIFY(repo.saveSummary("h1", summary("Second"), "{}", "m"));
  QCOMPARE(repo.summaries().size(), 1);
  QCOMPARE(repo.summaryTitle("h1"), QString("Second"));
  QCOMPARE(count(m_db, "SELECT COUNT(*) FROM Citations"), 1);
}

// Titles with quotes used to be pasted into the SQL by hand.
void TestLibraryRepository::summaryWithQuotes() {
  addChunk(m_db, "h'1");
  LibraryRepository repo(m_db);
  const QString title = "Workers' \"discipline\"; DROP TABLE x --";
  QVERIFY(repo.saveSummary("h'1", summary(title), "{}", "m"));
  SqliteStatement stmt = m_db.prepare(
      "SELECT clean_title FROM chunks WHERE paper_hash = 'h''1'");
  auto step = stmt.step();
  QVERIFY(step && *step);
  QCOMPARE(stmt.columnText(0), title);
}

void TestLibraryRepository::queries() {
  LibraryRepository repo(m_db);
  QVERIFY(repo.saveQuery("Q1", "A1", "m1"));
  QVERIFY(repo.saveQuery("Q2", "A2", "m2"));
  const QList<QueryRow> rows = repo.queries();
  QCOMPARE(rows.size(), 2);
  QCOMPARE(rows[0].queryText, QString("Q1"));
  QCOMPARE(rows[0].response, QString("A1"));
  QCOMPARE(rows[0].llmodel, QString("m1"));
  QVERIFY(rows[0].date.isValid());
  QCOMPARE(rows[1].queryText, QString("Q2"));
}

void TestLibraryRepository::removePapers() {
  addChunk(m_db, "h1");
  addChunk(m_db, "h2");
  LibraryRepository repo(m_db);
  QVERIFY(repo.saveSummary("h1", summary("One"), "{}", "m"));
  QVERIFY(repo.saveSummary("h2", summary("Two"), "{}", "m"));

  const BatchResult r = repo.removePapers({"h1"});
  QVERIFY(r.ok());
  QCOMPARE(repo.summaries().size(), 1);
  QCOMPARE(repo.summaries().first().paperHash, QString("h2"));
  QCOMPARE(count(m_db, "SELECT COUNT(*) FROM chunks WHERE paper_hash = 'h1'"),
           0);
  QCOMPARE(count(m_db, "SELECT COUNT(*) FROM chunks WHERE paper_hash = 'h2'"),
           1);
  QCOMPARE(count(m_db, "SELECT COUNT(*) FROM Citations"), 1);
  QVERIFY(repo.removePapers({}).ok());
}

void TestLibraryRepository::removeQueries() {
  LibraryRepository repo(m_db);
  QVERIFY(repo.saveQuery("Q1", "A1", "m"));
  QVERIFY(repo.saveQuery("Q2", "A2", "m"));
  const int first = repo.queries().first().id;
  QVERIFY(repo.removeQueries({first}).ok());
  QCOMPARE(repo.queries().size(), 1);
  QCOMPARE(repo.queries().first().queryText, QString("Q2"));
}

void TestLibraryRepository::indexedPapers() {
  addChunk(m_db, "h2");
  addChunk(m_db, "h1");
  const QList<IndexedPaper> papers = LibraryRepository(m_db).indexedPapers();
  QCOMPARE(papers.size(), 2);
  QCOMPARE(papers[0].paperHash, QString("h1"));
  QCOMPARE(papers[0].cleanTitle, QString("file.pdf"));
  QCOMPARE(papers[0].pages, 1);
}

namespace {

SummaryRow importedRow(const QString &hash, const QString &title) {
  SummaryRow row;
  row.paperHash = hash;
  row.title = title;
  row.pagesNum = 12;
  row.date = QDateTime::currentDateTimeUtc();
  return row;
}

} // namespace

// The papers of an import get their row, found again by their hash.
void TestLibraryRepository::addPapers() {
  LibraryRepository repo(m_db);
  QVERIFY(repo.addPapers({importedRow("h1", "a.pdf"), importedRow("h2", "b.pdf")}));
  QVERIFY(repo.addPapers({}));
  auto hashes = repo.paperHashes();
  QVERIFY(hashes);
  hashes->sort();
  QCOMPARE(*hashes, QStringList({"h1", "h2"}));
  const auto rows = repo.summaries();
  QCOMPARE(rows.size(), 2);
  QCOMPARE(rows[0].title, QString("a.pdf"));
  QCOMPARE(rows[0].pagesNum, 12);
}

// One paper that cannot be added (already there): none of the batch is.
void TestLibraryRepository::addPapersAllOrNone() {
  LibraryRepository repo(m_db);
  QVERIFY(repo.addPapers({importedRow("h1", "a.pdf")}));
  QVERIFY(!repo.addPapers({importedRow("h2", "b.pdf"), importedRow("h1", "again.pdf")}));
  QCOMPARE(*repo.paperHashes(), QStringList({"h1"}));
  // The connection is usable again (no transaction left open).
  QVERIFY(repo.addPapers({importedRow("h3", "c.pdf")}));
}

QTEST_GUILESS_MAIN(TestLibraryRepository)
#include "tst_libraryrepository.moc"
