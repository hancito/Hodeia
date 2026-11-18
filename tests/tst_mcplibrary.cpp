// Unit tests for McpLibrary, the MCP tools over the library: a temporary
// HodeIA data folder (config.json + Projects/<name>/<name>.db) with two
// papers, one summarized with OpenAlex data, one only ingested.

#include "libraryrepository.h"
#include "mcplibrary.h"
#include "projectstore.h"
#include "sqlitevec.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

namespace {

const QString kSummarized = "aaaaaaaaaaaa1111111111111111111a";
const QString kIngested = "bbbbbbbbbbbb2222222222222222222b";

void writeConfig(const QString &dir, const QString &project) {
  QFile f(dir + "/config.json");
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(QJsonDocument(QJsonObject{{"lastProject", project}}).toJson());
}

QString dbPath(const QString &dir, const QString &project) {
  return dir + "/Projects/" + project + "/" + project + ".db";
}

void addChunk(SqliteDb &db, const QString &hash, int page, int idx,
              const QString &text, const QString &title) {
  SqliteStatement stmt = db.prepare(
      "INSERT INTO chunks (embedding, paper_hash, page, chunk_idx, "
      "chunk_text, citation, clean_title, page_metadata) "
      "VALUES (vec_f32(?), ?, ?, ?, ?, ?, ?, '')");
  (void)stmt.bind(1, QByteArray(384 * int(sizeof(float)), '\0'));
  (void)stmt.bind(2, hash);
  (void)stmt.bind(3, page);
  (void)stmt.bind(4, idx);
  (void)stmt.bind(5, text);
  (void)stmt.bind(6, title);
  (void)stmt.bind(7, title);
  QVERIFY(stmt.exec().has_value());
}

/// A project "Lib" with the two papers.
void makeLibrary(const QString &dir) {
  QDir().mkpath(dir + "/Projects/Lib");
  writeConfig(dir, "Lib");
  SqliteDb db;
  QVERIFY(ProjectStore::open(db, dbPath(dir, "Lib")).has_value());
  addChunk(db, kSummarized, 0, 0, "Page one text.", "a.pdf");
  addChunk(db, kSummarized, 2, 1, "Page three text.", "a.pdf");
  addChunk(db, kIngested, 0, 0, "Other paper.",
           "Carlin - 1989 - Why shirking varies");
  PaperSummary s;
  s.title = "Hybrid Choice Models";
  s.authors = {"J. Walker", "M. Ben-Akiva"};
  s.year = 2002;
  s.journal = "Math. Soc. Sci.";
  s.researchObjective = "Objective.";
  s.coreFindingsAndSignificance = {"Finding one"};
  const QString json = R"({"title":"Hybrid Choice Models","authors":["J. )"
                       R"(Walker","M. Ben-Akiva"],"year":2002,"journal":)"
                       R"("Math. Soc. Sci.","researchObjective":"Objective.",)"
                       R"("coreFindingsAndSignificance":["Finding one"]})";
  QVERIFY(LibraryRepository(db).saveSummary(kSummarized, s, json, "model-x"));
  // OpenAlex data: one work citing the paper.
  const QJsonArray citing{QJsonObject{
      {"display_name", "A citing work"},
      {"authors", QString("Doe, Jane") + QChar(0x1F) + "Roe, Rick"},
      {"publication_year", 2010},
      {"journal", "J. Econ."},
      {"doi", "https://doi.org/10.1/x"}}};
  SqliteStatement up = db.prepare(
      "UPDATE Citations SET cites = ?, cites_count = 1, cited_by = '[]', "
      "cited_by_count = 0 WHERE paper_hash = ?");
  (void)up.bind(1, QString::fromUtf8(
                       QJsonDocument(citing).toJson(QJsonDocument::Compact)));
  (void)up.bind(2, kSummarized);
  QVERIFY(up.exec().has_value());
  // Ingestion also adds a Summaries row, title = file name, no JSON.
  QVERIFY(db.exec("INSERT INTO Summaries (paper_hash, title, pages_num) "
                  "VALUES ('" + kIngested + "', 'Carlin.pdf', 1)"));
}

} // namespace

class TestMcpLibrary : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();
  void listPapers();
  void listFilter();
  void getSummary();
  void getSummaryNotSummarized();
  void getPage();
  void getCitations();
  void paperIds();
  void followsProjectSwitch();
  void missingProject();
  void otherSchemaVersion();
  void search();

private:
  QTemporaryDir *m_dir = nullptr;
  McpLibrary *m_lib = nullptr;
};

void TestMcpLibrary::init() {
  m_dir = new QTemporaryDir;
  makeLibrary(m_dir->path());
  m_lib = new McpLibrary({m_dir->filePath("config.json"),
                          m_dir->filePath("Projects"), QString()});
}

void TestMcpLibrary::cleanup() {
  delete m_lib;
  delete m_dir;
}

void TestMcpLibrary::listPapers() {
  const auto out = m_lib->listPapers({});
  QVERIFY(out.has_value());
  QVERIFY(out->contains("Project \"Lib\": 2 paper(s)"));
  QVERIFY2(out->contains("[@aaaaaaaaaaaa] J. Walker; M. Ben-Akiva (2002). "
                         "Hybrid Choice Models. Math. Soc. Sci. 3 pages. "
                         "Summary: yes."),
           qPrintable(*out));
  // Ingested only: labelled from its file name, not "summarized".
  QVERIFY(out->contains("[@bbbbbbbbbbbb] Carlin (1989) — Why shirking varies. "
                        "1 pages. Summary: no"));
}

void TestMcpLibrary::listFilter() {
  const auto out = m_lib->listPapers({{"filter", "SHIRKING"}});
  QVERIFY(out->contains("1 paper(s) matching"));
  QVERIFY(!out->contains("Hybrid"));
  QVERIFY(m_lib->listPapers({{"filter", "nothing like it"}})
              ->contains("No paper matches"));
}

void TestMcpLibrary::getSummary() {
  const auto out = m_lib->getSummary({{"paper", "aaaaaaaaaaaa"}});
  QVERIFY(out.has_value());
  QVERIFY(out->startsWith("[@aaaaaaaaaaaa] Hybrid Choice Models\n"));
  QVERIFY(out->contains("Authors: J. Walker; M. Ben-Akiva"));
  QVERIFY(out->contains("## Research objective\nObjective."));
  QVERIFY(out->contains("- Finding one"));
  QVERIFY(out->contains("model-x"));
}

void TestMcpLibrary::getSummaryNotSummarized() {
  const auto out = m_lib->getSummary({{"paper", "bbbbbbbbbbbb"}});
  QVERIFY(!out.has_value());
  QVERIFY(out.error().contains("no summary yet"));
}

void TestMcpLibrary::getPage() {
  auto out = m_lib->getPage({{"paper", "[@aaaaaaaaaaaa:p3]"}, {"page", 3}});
  QVERIFY(out.has_value());
  QVERIFY(out->startsWith("[@aaaaaaaaaaaa:p3] Walker & Ben-Akiva (2002) — "
                          "Hybrid Choice Models, p. 3 (of 3)"));
  QVERIFY(out->endsWith("Page three text.\n"));
  // Page 2 has no text; page 4 does not exist.
  QVERIFY(m_lib->getPage({{"paper", "aaaaaaaaaaaa"}, {"page", 2}})
              ->contains("No text was extracted"));
  out = m_lib->getPage({{"paper", "aaaaaaaaaaaa"}, {"page", 4}});
  QVERIFY(!out.has_value());
  QVERIFY(out.error().contains("between 1 and 3"));
}

void TestMcpLibrary::getCitations() {
  const auto out = m_lib->getCitations({{"paper", "aaaaaa"}});
  QVERIFY(out.has_value());
  QVERIFY(out->contains("References (works this paper cites) (0 on "
                        "OpenAlex)\nNone found."));
  QVERIFY2(out->contains("- Doe, Jane & Roe, Rick (2010). A citing work. J. "
                         "Econ. [https://doi.org/10.1/x](https://doi.org/10.1/"
                         "x)"),
           qPrintable(*out));
  // No Citations row for a paper never summarized.
  QVERIFY(!m_lib->getCitations({{"paper", "bbbbbb"}}).has_value());
}

void TestMcpLibrary::paperIds() {
  QVERIFY(m_lib->getPage({{"paper", "AAAAAAAAAAAA"}, {"page", 1}}));
  auto out = m_lib->getPage({{"paper", "abc"}, {"page", 1}});
  QVERIFY(out.error().contains("too short"));
  out = m_lib->getPage({{"paper", "cccccc"}, {"page", 1}});
  QVERIFY(out.error().contains("No paper has the id"));
  out = m_lib->getPage({{"paper", "not an id"}, {"page", 1}});
  QVERIFY(out.error().contains("is not a paper id"));
}

// Switching project in HodeIA (config.json) is followed on the next call.
void TestMcpLibrary::followsProjectSwitch() {
  QVERIFY(m_lib->listPapers({})->contains("\"Lib\": 2"));
  QDir().mkpath(m_dir->filePath("Projects/Empty"));
  {
    SqliteDb db;
    QVERIFY(ProjectStore::open(db, dbPath(m_dir->path(), "Empty")));
  }
  writeConfig(m_dir->path(), "Empty");
  QVERIFY(m_lib->listPapers({})->contains("\"Empty\" has no papers"));
}

// Never create a database for a project that has none.
void TestMcpLibrary::missingProject() {
  writeConfig(m_dir->path(), "Gone");
  const auto out = m_lib->listPapers({});
  QVERIFY(!out.has_value());
  QVERIFY(out.error().contains("was not found"));
  QVERIFY(!QFile::exists(dbPath(m_dir->path(), "Gone")));
}

// Another schema version: refused, and never migrated from here.
void TestMcpLibrary::otherSchemaVersion() {
  {
    SqliteDb db;
    QVERIFY(db.open(dbPath(m_dir->path(), "Lib")));
    QVERIFY(db.exec("PRAGMA user_version = 0"));
  }
  const auto out = m_lib->listPapers({});
  QVERIFY(!out.has_value());
  QVERIFY(out.error().contains("open the project in HodeIA"));
  SqliteDb db;
  QVERIFY(db.open(dbPath(m_dir->path(), "Lib")));
  QCOMPARE(ProjectStore::schemaVersion(db), 0);
}

// Real embeddings: the passage about the query comes first.
void TestMcpLibrary::search() {
  const QString model = QStringLiteral(HODEIA_E5_MODEL);
  if (!QFile::exists(model))
    QSKIP("E5 model not available (Git LFS)");
  const QString dir = m_dir->filePath("search");
  QDir().mkpath(dir + "/Projects/S");
  writeConfig(dir, "S");
  {
    SqliteDb db;
    QVERIFY(ProjectStore::open(db, dbPath(dir, "S")));
    SqliteVecManager embeddings(nullptr);
    QVERIFY(embeddings.initialize(model));
    TextChunk a{"Absenteeism and peer effects: employees are absent from work "
                "more often when their co-workers are often absent. Sickness "
                "absence spreads within work teams through peer pressure and "
                "group norms about acceptable absence.",
                kSummarized, 4, 0, "Peer absence", "Smith (2020)", ""};
    TextChunk b{"Monetary policy: the central bank raised its key interest "
                "rate by half a point to bring inflation back to its target, "
                "and bond yields rose across the euro area.",
                kIngested, 0, 0, "Monetary policy", "Jones (2019)", ""};
    QCOMPARE(embeddings.insertChunks(db, {a, b}), 2);
  }
  McpLibrary lib({dir + "/config.json", dir + "/Projects", model});
  const auto out =
      lib.searchLibrary({{"query", "Are employees absent from work more often "
                                   "when their co-workers are absent?"},
                         {"k", 1}});
  QVERIFY2(out.has_value(), qPrintable(out ? QString() : out.error()));
  QVERIFY2(out->contains("[@aaaaaaaaaaaa:p5] Smith (2020) — Peer absence, p. 5"),
           qPrintable(*out));
  QVERIFY(!out->contains("central bank"));
  QVERIFY(!lib.searchLibrary({}).has_value()); // query is required
}

QTEST_GUILESS_MAIN(TestMcpLibrary)
#include "tst_mcplibrary.moc"
