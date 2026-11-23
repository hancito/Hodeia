// Unit tests for LibraryRepository, on a temporary project database created
// by ProjectStore (real schema, sqlite-vec chunks table).

#include "libraryrepository.h"
#include "projectstore.h"
#include "sqlitedb.h"

#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

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

const QByteArray kEmbedding(384 * int(sizeof(float)), '\0');

TextChunk chunk(const QString &hash, int idx = 0) {
  return TextChunk{"text", hash, 0, idx, "file.pdf", "file.pdf", QString()};
}

void addChunk(SqliteDb &db, const QString &hash) {
  auto r = LibraryRepository(db).addChunks({chunk(hash)}, {kEmbedding});
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
  void addIndexedPaper();
  void addIndexedPaperAllOrNothing();
  void nearestChunks();
  void chunksOfPaper();
  void onePaper();
  void summariesSignature();

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

// An imported paper gets its row and its chunks, found again by its hash.
void TestLibraryRepository::addIndexedPaper() {
  LibraryRepository repo(m_db);
  QVERIFY(repo.addIndexedPaper(importedRow("h1", "a.pdf"),
                               {chunk("h1", 0), chunk("h1", 1)},
                               {kEmbedding, kEmbedding}));
  QVERIFY(repo.addIndexedPaper(importedRow("h2", "b.pdf"), {chunk("h2")},
                               {kEmbedding}));
  auto hashes = repo.paperHashes();
  QVERIFY(hashes);
  hashes->sort();
  QCOMPARE(*hashes, QStringList({"h1", "h2"}));
  const auto rows = repo.summaries();
  QCOMPARE(rows.size(), 2);
  QCOMPARE(rows[0].title, QString("a.pdf"));
  QCOMPARE(rows[0].pagesNum, 12);
  QCOMPARE(count(m_db, "SELECT COUNT(*) FROM chunks WHERE paper_hash = 'h1'"),
           2);
  QCOMPARE(repo.indexedPapers().size(), 2);
}

// A paper whose row or chunks cannot be stored leaves nothing behind, and
// the connection is usable again (no transaction left open).
void TestLibraryRepository::addIndexedPaperAllOrNothing() {
  LibraryRepository repo(m_db);
  QVERIFY(repo.addIndexedPaper(importedRow("h1", "a.pdf"), {chunk("h1")},
                               {kEmbedding}));

  // Already there: neither the row nor the chunks are added again.
  QVERIFY(!repo.addIndexedPaper(importedRow("h1", "again.pdf"), {chunk("h1")},
                                {kEmbedding}));
  QCOMPARE(count(m_db, "SELECT COUNT(*) FROM chunks"), 1);

  // A chunk that cannot be stored (wrong embedding size): no row either.
  QVERIFY(!repo.addIndexedPaper(importedRow("h2", "b.pdf"),
                                {chunk("h2", 0), chunk("h2", 1)},
                                {kEmbedding, QByteArray(8, '\0')}));
  // Fewer embeddings than chunks: refused, no row.
  QVERIFY(!repo.addIndexedPaper(importedRow("h3", "c.pdf"),
                                {chunk("h3", 0), chunk("h3", 1)},
                                {kEmbedding}));
  QCOMPARE(*repo.paperHashes(), QStringList({"h1"}));
  QCOMPARE(count(m_db, "SELECT COUNT(*) FROM chunks"), 1);

  QVERIFY(repo.addIndexedPaper(importedRow("h4", "d.pdf"), {chunk("h4")},
                               {kEmbedding}));
  QCOMPARE(count(m_db, "SELECT COUNT(*) FROM chunks"), 2);
}

namespace {

/// A unit vector along `axis`, plus `tilt` along axis 0 (normalized).
QByteArray direction(int axis, float tilt = 0.0f) {
  QVector<float> v(384, 0.0f);
  v[axis] = 1.0f;
  v[0] += tilt;
  float norm = 0;
  for (float x : v)
    norm += x * x;
  for (float &x : v)
    x /= std::sqrt(norm);
  return QByteArray(reinterpret_cast<const char *>(v.constData()),
                    v.size() * qsizetype(sizeof(float)));
}

TextChunk chunkAt(const QString &hash, int page, int idx, const QString &text) {
  return TextChunk{text, hash, page, idx, "Title " + hash, "Cite " + hash, "{}"};
}

} // namespace

// Nearest first, at most k, optionally within some papers.
void TestLibraryRepository::nearestChunks() {
  LibraryRepository repo(m_db);
  QVERIFY(repo.addChunks({chunkAt("a", 0, 0, "near"), chunkAt("a", 1, 1, "far"),
                          chunkAt("b", 0, 0, "middle")},
                         {direction(1, 0.1f), direction(3), direction(1, 0.8f)}));
  const QByteArray query = direction(1);

  const auto all = repo.nearestChunks(query, 10);
  QCOMPARE(all.size(), 3);
  QCOMPARE(all[0].text, QString("near"));
  QCOMPARE(all[1].text, QString("middle"));
  QCOMPARE(all[2].text, QString("far"));
  QVERIFY(all[0].distance < all[1].distance);
  QCOMPARE(all[0].citation, QString("Cite a"));
  QCOMPARE(all[0].cleanTitle, QString("Title a"));
  QCOMPARE(all[0].pageMetadata, QString("{}"));

  QCOMPARE(repo.nearestChunks(query, 1).size(), 1);

  // Within paper b only, then both papers merged by distance and cut at k.
  const auto onlyB = repo.nearestChunks(query, 10, {"b"});
  QCOMPARE(onlyB.size(), 1);
  QCOMPARE(onlyB[0].text, QString("middle"));
  const auto both = repo.nearestChunks(query, 2, {"b", "a"});
  QCOMPARE(both.size(), 2);
  QCOMPARE(both[0].text, QString("near"));
  QCOMPARE(both[1].text, QString("middle"));

  QVERIFY(repo.nearestChunks(QByteArray(), 10).isEmpty());
}

// A paper's chunks by page then position, whatever the insertion order.
void TestLibraryRepository::chunksOfPaper() {
  LibraryRepository repo(m_db);
  QVERIFY(repo.addChunks({chunkAt("a", 1, 2, "p1c2"), chunkAt("a", 0, 0, "p0c0"),
                          chunkAt("b", 0, 0, "other"), chunkAt("a", 1, 1, "p1c1")},
                         {kEmbedding, kEmbedding, kEmbedding, kEmbedding}));
  const auto list = repo.chunks("a");
  QCOMPARE(list.size(), 3);
  QCOMPARE(list[0].text, QString("p0c0"));
  QCOMPARE(list[1].text, QString("p1c1"));
  QCOMPARE(list[2].text, QString("p1c2"));
  QCOMPARE(list[2].page, 1);
  QCOMPARE(list[2].chunkIdx, 2);
  QCOMPARE(list[0].distance, 0.0f);
  QVERIFY(repo.chunks("unknown").isEmpty());
  QVERIFY(repo.chunks(QString()).isEmpty());
}

// The reads of one paper give what the full lists give for it.
void TestLibraryRepository::onePaper() {
  LibraryRepository repo(m_db);
  QVERIFY(repo.addIndexedPaper(importedRow("abc123", "a.pdf"),
                               {chunkAt("abc123", 0, 0, "x"),
                                chunkAt("abc123", 4, 1, "y")},
                               {kEmbedding, kEmbedding}));
  QVERIFY(repo.addIndexedPaper(importedRow("abd456", "b.pdf"),
                               {chunkAt("abd456", 0, 0, "z")}, {kEmbedding}));
  QVERIFY(repo.saveSummary("abc123", summary("Hybrid Choice"), "{}", "m"));

  const auto s = repo.summary("abc123");
  QVERIFY(s);
  QCOMPARE(s->title, QString("Hybrid Choice"));
  QCOMPARE(s->pagesNum, 12); // kept from the import
  QVERIFY(!repo.summary("nope"));

  const auto c = repo.citation("abc123"); // registered by saveSummary
  QVERIFY(c);
  QCOMPARE(c->paperHash, QString("abc123"));
  QVERIFY(!repo.citation("abd456")); // never summarized

  const auto p = repo.indexedPaper("abc123");
  QVERIFY(p);
  QCOMPARE(p->pages, 5);
  QCOMPARE(p->cleanTitle, QString("Hybrid Choice"));
  QVERIFY(!repo.indexedPaper("nope"));

  QCOMPARE(repo.paperHashesStartingWith("ab"),
           QStringList({"abc123", "abd456"}));
  QCOMPARE(repo.paperHashesStartingWith("abc"), QStringList({"abc123"}));
  QVERIFY(repo.paperHashesStartingWith("x").isEmpty());
  QVERIFY(repo.paperHashesStartingWith("").isEmpty());
}

// Changes with a summary, not with an import; another connection's commit
// changes data_version, the connection's own does not.
void TestLibraryRepository::summariesSignature() {
  LibraryRepository repo(m_db);
  const QString empty = repo.summariesSignature();
  const int64_t version = m_db.dataVersion();
  QVERIFY(version >= 0);

  SqliteDb other;
  QVERIFY(other.open(m_dir->filePath("P.db")).has_value());
  QVERIFY(LibraryRepository(other).addIndexedPaper(
      importedRow("h1", "a.pdf"), {chunkAt("h1", 0, 0, "x")}, {kEmbedding}));
  QVERIFY(m_db.dataVersion() != version);
  QCOMPARE(repo.summariesSignature(), empty); // an import

  QVERIFY(LibraryRepository(other).saveSummary("h1", summary("T"), "{}", "m"));
  const QString one = repo.summariesSignature();
  QVERIFY(one != empty);

  // Replaced within the same second: still noticed.
  QVERIFY(LibraryRepository(other).saveSummary("h1", summary("T"), "{\"a\":1}", "m"));
  QVERIFY(repo.summariesSignature() != one);

  const int64_t mine = m_db.dataVersion();
  QVERIFY(repo.saveSummary("h1", summary("T2"), "{}", "m"));
  QCOMPARE(m_db.dataVersion(), mine);
}

QTEST_GUILESS_MAIN(TestLibraryRepository)
#include "tst_libraryrepository.moc"
