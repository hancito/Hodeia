// Unit tests for CitationRepository: the OpenAlex state of the Citations
// table (papers to fetch, matches, lists) in a project database.

#include "citationrepository.h"
#include "projectstore.h"
#include "sqlitedb.h"

#include <QTemporaryDir>
#include <QtTest>

class TestCitationRepository : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();
  void pendingFromSummary();
  void pendingFromZotero();
  void pendingPrefersZoteroRowWithDoi();
  void pendingSkipsRecentNotFound();
  void registerDoiPapers();
  void saves();

private:
  void addSummary(const QString &hash, const QString &title, bool summarized = true);
  void addCitation(const QString &hash, const QString &extra = QString());
  void addZotero(int library, const QString &attachment, const QString &hash,
                 const QString &doi, bool removed = false);
  QString value(const QString &sql);

  QTemporaryDir *m_dir = nullptr;
  SqliteDb m_db;
};

void TestCitationRepository::init() {
  m_dir = new QTemporaryDir;
  QVERIFY(ProjectStore::open(m_db, m_dir->filePath("P.db")).has_value());
}

void TestCitationRepository::cleanup() {
  m_db.close();
  delete m_dir;
  m_dir = nullptr;
}

void TestCitationRepository::addSummary(const QString &hash, const QString &title,
                                        bool summarized) {
  QVERIFY(m_db.exec(QString("INSERT INTO Summaries (paper_hash, title, authors, "
                            "pages_num, journal, year, summary_json) VALUES ('%1', "
                            "'%2', 'Avinash Dixit; Barry Nalebuff', 10, 'EJ', '1980', '%3')")
                        .arg(hash, title, summarized ? "{}" : "")));
}

// A Citations row with empty lists; `extra`: "column = value, ..." to set.
void TestCitationRepository::addCitation(const QString &hash, const QString &extra) {
  QVERIFY(m_db.exec(QString("INSERT INTO Citations (paper_hash, clean_title) "
                            "VALUES ('%1', 'file.pdf')")
                        .arg(hash)));
  if (!extra.isEmpty())
    QVERIFY(m_db.exec(QString("UPDATE Citations SET %1 WHERE paper_hash = '%2'")
                          .arg(extra, hash)));
}

void TestCitationRepository::addZotero(int library, const QString &attachment,
                                       const QString &hash, const QString &doi,
                                       bool removed) {
  QVERIFY(m_db.exec(
      QString("INSERT INTO ZoteroItems (library_id, item_key, attachment_key, "
              "paper_hash, title, authors, year, journal, doi, removed_at) VALUES "
              "(%1, 'I', '%2', '%3', 'Zotero title', '[\"Dixit, Avinash\", \"OECD\"]', "
              "'1979', 'Bell J.', '%4', %5)")
          .arg(library)
          .arg(attachment, hash, doi, removed ? "CURRENT_TIMESTAMP" : "NULL")));
}

QString TestCitationRepository::value(const QString &sql) {
  SqliteStatement s = m_db.prepare(sql);
  auto step = s.step();
  return step && *step ? s.columnText(0) : QString("<none>");
}

// A summarized paper is searched with its summary's metadata.
void TestCitationRepository::pendingFromSummary() {
  addSummary("h1", "Entry deterrence");
  addCitation("h1", "work_id = 'W1', cites = '[]'");
  addCitation("done", "cites = '[]', cited_by = '[]'"); // nothing missing
  const auto pending = CitationRepository(m_db).pending(30);
  QVERIFY(pending);
  QCOMPARE(pending->size(), 1);
  const PendingCitation &p = pending->first();
  QCOMPARE(p.paperHash, QString("h1"));
  QCOMPARE(p.workId, QString("W1"));
  QVERIFY(!p.needCites);
  QVERIFY(p.needCitedBy);
  QCOMPARE(p.title, QString("Entry deterrence"));
  QCOMPARE(p.authors, QStringList({"Avinash Dixit", "Barry Nalebuff"}));
  QCOMPARE(p.year, 1980);
  QCOMPARE(p.journal, QString("EJ"));
  QVERIFY(p.doi.isEmpty());
}

// Not summarized yet: Zotero's metadata, its names as "First Last".
void TestCitationRepository::pendingFromZotero() {
  addSummary("h1", "", false);
  addCitation("h1");
  addZotero(1, "A", "h1", "10.2307/3003181");
  const auto pending = CitationRepository(m_db).pending(30);
  QVERIFY(pending);
  QCOMPARE(pending->size(), 1);
  const PendingCitation &p = pending->first();
  QCOMPARE(p.title, QString("Zotero title"));
  QCOMPARE(p.authors, QStringList({"Avinash Dixit", "OECD"}));
  QCOMPARE(p.year, 1979);
  QCOMPARE(p.journal, QString("Bell J."));
  QCOMPARE(p.doi, QString("10.2307/3003181"));
}

// Among the Zotero rows of a paper: one with a DOI, still in Zotero, first.
void TestCitationRepository::pendingPrefersZoteroRowWithDoi() {
  addSummary("h1", "T");
  addCitation("h1");
  addZotero(1, "A1", "h1", "");
  addZotero(1, "A2", "h1", "10.1/removed", true);
  addZotero(2, "A3", "h1", "10.1/kept");
  const auto pending = CitationRepository(m_db).pending(30);
  QVERIFY(pending);
  QCOMPARE(pending->size(), 1);
  QCOMPARE(pending->first().doi, QString("10.1/kept"));
}

// Not found less than retryDays ago: skipped, unless a DOI has appeared
// since a search without one.
void TestCitationRepository::pendingSkipsRecentNotFound() {
  for (const char *h : {"recent", "recentDoi", "doiNow", "old"})
    addSummary(h, h);
  addCitation("recent", "lookup_status = 'not_found', lookup_at = datetime('now', '-1 days')");
  addCitation("recentDoi",
              "lookup_status = 'not_found_doi', lookup_at = datetime('now', '-1 days')");
  addZotero(1, "A1", "recentDoi", "10.1/a");
  addCitation("doiNow", "lookup_status = 'not_found', lookup_at = datetime('now', '-1 days')");
  addZotero(1, "A2", "doiNow", "10.1/b");
  addCitation("old", "lookup_status = 'not_found_doi', lookup_at = datetime('now', '-40 days')");
  const auto pending = CitationRepository(m_db).pending(30);
  QVERIFY(pending);
  QStringList hashes;
  for (const PendingCitation &p : *pending)
    hashes << p.paperHash;
  hashes.sort();
  QCOMPARE(hashes, QStringList({"doiNow", "old"}));
}

// Papers of the project with a DOI in Zotero get a row, once.
void TestCitationRepository::registerDoiPapers() {
  addSummary("withDoi", "", false);
  addZotero(1, "A1", "withDoi", "10.1/a");
  addZotero(2, "A2", "withDoi", "10.1/a"); // same PDF, two libraries: one row
  addSummary("noDoi", "", false);
  addZotero(1, "A3", "noDoi", "");
  addZotero(1, "A4", "notImported", "10.1/c"); // no Summaries row
  addSummary("known", "Known");
  addCitation("known");
  addZotero(1, "A5", "known", "10.1/d");

  CitationRepository repo(m_db);
  const auto added = repo.registerDoiPapers();
  QVERIFY(added);
  QCOMPARE(*added, 1);
  QCOMPARE(value("SELECT clean_title FROM Citations WHERE paper_hash = 'withDoi'"),
           QString("Zotero title"));
  QCOMPARE(value("SELECT clean_title FROM Citations WHERE paper_hash = 'known'"),
           QString("file.pdf"));
  QCOMPARE(*repo.registerDoiPapers(), 0);
}

void TestCitationRepository::saves() {
  addCitation("h1");
  addCitation("h2");
  CitationRepository repo(m_db);

  QVERIFY(repo.saveWorkId("h1", "W42"));
  QCOMPARE(value("SELECT work_id || ' ' || lookup_status || ' ' || (lookup_at IS NOT NULL) "
                 "FROM Citations WHERE paper_hash = 'h1'"),
           QString("W42 matched 1"));

  QVERIFY(repo.markNotFound("h2", false));
  QCOMPARE(value("SELECT lookup_status FROM Citations WHERE paper_hash = 'h2'"),
           QString("not_found"));
  QVERIFY(repo.markNotFound("h2", true));
  QCOMPARE(value("SELECT lookup_status FROM Citations WHERE paper_hash = 'h2'"),
           QString("not_found_doi"));

  QVERIFY(repo.saveList("h1", CitationList::Cites, "[1]", 7));
  QVERIFY(repo.saveList("h1", CitationList::CitedBy, "[2]", 9));
  QCOMPARE(value("SELECT cites || cites_count || cited_by || cited_by_count "
                 "FROM Citations WHERE paper_hash = 'h1'"),
           QString("[1]7[2]9"));
}

QTEST_GUILESS_MAIN(TestCitationRepository)
#include "tst_citationrepository.moc"
