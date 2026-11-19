#include "mcplibrary.h"

#include "apppaths.h"
#include "citationformat.h"
#include "libraryrepository.h"
#include "mcpserver.h"
#include "metadataresolver.h"
#include "projectstore.h"
#include "retriever.h"
#include "sqlitevec.h"
#include "summaryformat.h"
#include "summarypromptbuilder.h"
#include "zoterorepository.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <algorithm>

namespace {

constexpr int kDefaultResults = 8;
constexpr int kMaxResults = 30;
constexpr int kMaxPapers = 20; // papers a search can be restricted to
// Token budget the search fills before keeping the best pages: room for
// kMaxResults pages of a few chunks each.
constexpr int kSearchBudgetTokens = 24000;

QJsonObject schema(const QJsonObject &properties,
                   const QStringList &required = {}) {
  QJsonObject s{{"type", "object"}, {"properties", properties}};
  if (!required.isEmpty())
    s["required"] = QJsonArray::fromStringList(required);
  return s;
}

QJsonObject stringProp(const QString &description) {
  return {{"type", "string"}, {"description", description}};
}

QJsonObject intProp(const QString &description, int min, int max) {
  return {{"type", "integer"},
          {"description", description},
          {"minimum", min},
          {"maximum", max}};
}

QJsonObject stringArrayProp(const QString &description, int maxItems) {
  return {{"type", "array"},
          {"description", description},
          {"items", QJsonObject{{"type", "string"}}},
          {"maxItems", maxItems}};
}

const QString kPaperIdHelp = QStringLiteral(
    "Paper id: the id shown in brackets by list_papers or search_library, "
    "e.g. \"95f8ed154398\" or \"[@95f8ed154398]\" (a unique hash prefix of at "
    "least 6 characters also works).");

QString bulletList(const QStringList &items) {
  QString out;
  for (const QString &item : items)
    out += "- " + item + "\n";
  return out;
}

} // namespace

McpLibrary::Paths McpLibrary::defaultPaths() {
  return {AppPaths::configFile(), AppPaths::projectsRoot(),
          AppPaths::embeddingModel()};
}

McpLibrary::McpLibrary(Paths paths)
    : m_paths(std::move(paths)),
      m_embeddings(std::make_unique<SqliteVecManager>(nullptr)) {}

// The embedding service unloads its model when destroyed.
McpLibrary::~McpLibrary() = default;

/* ── Project ────────────────────────────────────────────────────── */

std::expected<void, QString> McpLibrary::openActiveProject() {
  QFile file(m_paths.configFile);
  if (!file.open(QIODevice::ReadOnly))
    return std::unexpected(QStringLiteral(
        "HodeIA's settings were not found: start HodeIA once and open a "
        "project."));
  const QJsonObject config = QJsonDocument::fromJson(file.readAll()).object();
  const QString project = config.value("lastProject").toString();
  // Like the app: Zotero's title, authors, year and journal first.
  m_zoteroPriority = config.value("zoteroPriority").toBool(false);
  if (project.isEmpty())
    return std::unexpected(
        QStringLiteral("No project is active in HodeIA: open one there."));
  if (project == m_project && m_db.isOpen())
    return {};

  m_db.close();
  m_project.clear();
  const QString path =
      m_paths.projectsRoot + "/" + project + "/" + project + ".db";
  if (!QFile::exists(path))
    return std::unexpected(
        QString("The database of project \"%1\" was not found.").arg(project));
  if (auto r = m_db.open(path); !r)
    return std::unexpected(QString("The database of project \"%1\" could not "
                                   "be opened: %2")
                               .arg(project, r.error()));
  // Never migrate from here: the GUI app owns the schema.
  const int version = ProjectStore::schemaVersion(m_db);
  if (version != ProjectStore::currentVersion()) {
    m_db.close();
    return std::unexpected(
        QString("Project \"%1\" has database version %2 but this HodeIA "
                "reads version %3: open the project in HodeIA (same version "
                "as this server) first.")
            .arg(project)
            .arg(version)
            .arg(ProjectStore::currentVersion()));
  }
  m_project = project;
  qInfo() << "[MCP] Serving project" << project;
  return {};
}

std::expected<QString, QString> McpLibrary::resolvePaper(const QString &id) {
  static const QRegularExpression mark(
      QStringLiteral("^\\[?@?([0-9a-fA-F]+)(?::p\\d+)?\\]?$"));
  const auto m = mark.match(id.trimmed());
  if (!m.hasMatch())
    return std::unexpected(QString("\"%1\" is not a paper id. %2")
                               .arg(id, kPaperIdHelp));
  const QString prefix = m.captured(1).toLower();
  if (prefix.size() < 6)
    return std::unexpected(QString("Paper id \"%1\" is too short. %2")
                               .arg(id, kPaperIdHelp));

  const LibraryRepository repo(m_db);
  QStringList matches;
  for (const IndexedPaper &p : repo.indexedPapers())
    if (p.paperHash.startsWith(prefix) && !matches.contains(p.paperHash))
      matches.append(p.paperHash);
  for (const SummaryRow &s : repo.summaries())
    if (s.paperHash.startsWith(prefix) && !matches.contains(s.paperHash))
      matches.append(s.paperHash);
  if (matches.isEmpty())
    return std::unexpected(
        QString("No paper has the id \"%1\": use list_papers.").arg(id));
  if (matches.size() > 1)
    return std::unexpected(
        QString("The id \"%1\" matches %2 papers: give more characters.")
            .arg(id)
            .arg(matches.size()));
  return matches.first();
}

QHash<QString, ZoteroMeta> McpLibrary::zoteroMeta() {
  return m_zoteroPriority ? ZoteroRepository(m_db).metaByHash()
                          : QHash<QString, ZoteroMeta>();
}

QString McpLibrary::label(const QString &paperHash) {
  const LibraryRepository repo(m_db);
  PaperLabel l;
  bool found = false;
  for (const IndexedPaper &p : repo.indexedPapers())
    if (p.paperHash == paperHash) {
      l = MetadataResolver::paperLabel(paperHash, p.cleanTitle, p.citation);
      found = true;
      break;
    }
  if (!found)
    l = MetadataResolver::paperLabel(paperHash, repo.summaryTitle(paperHash),
                                     QString());
  const auto zotero = zoteroMeta();
  if (const auto it = zotero.constFind(paperHash); it != zotero.constEnd())
    l = MetadataResolver::withZotero(l, *it);
  return l.sourceForm();
}

/* ── Tools ──────────────────────────────────────────────────────── */

std::expected<QString, QString>
McpLibrary::searchLibrary(const QJsonObject &args) {
  const QString query = args.value("query").toString().trimmed();
  if (query.isEmpty())
    return std::unexpected(QStringLiteral("query is required."));
  const int k =
      std::clamp(args.value("k").toInt(kDefaultResults), 1, kMaxResults);
  if (auto r = openActiveProject(); !r)
    return std::unexpected(r.error());

  // Optional restriction to some papers, checked before loading the model.
  Retriever::Options options;
  const QJsonValue papersArg = args.value("papers");
  if (!papersArg.isUndefined() && !papersArg.isNull()) {
    if (!papersArg.isArray())
      return std::unexpected(QStringLiteral("papers must be a list of paper ids."));
    const QJsonArray ids = papersArg.toArray();
    if (ids.size() > kMaxPapers)
      return std::unexpected(
          QString("papers: at most %1 papers at once.").arg(kMaxPapers));
    for (const QJsonValue &id : ids) {
      const auto hash = resolvePaper(id.toString());
      if (!hash)
        return std::unexpected(hash.error());
      if (!options.papers.contains(*hash))
        options.papers.append(*hash);
    }
  }

  if (!m_modelLoaded) {
    if (m_paths.embeddingModel.isEmpty())
      return std::unexpected(QStringLiteral(
          "The embedding model was not found: start HodeIA once so it "
          "installs its models."));
    if (!m_embeddings->initialize(m_paths.embeddingModel))
      return std::unexpected("The embedding model could not be loaded: " +
                             m_embeddings->lastError());
    m_modelLoaded = true;
  }

  options.budgetTokens = kSearchBudgetTokens;
  QList<PageContext> pages =
      Retriever(m_db, *m_embeddings).retrieve(query, options);
  if (pages.size() > k)
    pages.resize(k);
  MetadataResolver::attachZotero(pages, zoteroMeta());
  const QString scope =
      options.papers.isEmpty()
          ? QStringLiteral("the library")
          : QString("%1 paper(s)").arg(options.papers.size());
  if (pages.isEmpty())
    return QString("No passage of %1 matches \"%2\".").arg(scope, query);

  QString out = QString("%1 page(s) of %2 for \"%3\", most "
                        "relevant first. Cite them with their mark.\n")
                    .arg(pages.size())
                    .arg(scope, query);
  for (const PageContext &pc : pages) {
    out += "\n" + CitationFormat::pageMark(pc.paperHash, pc.displayPage()) +
           " " + MetadataResolver::paperLabel(pc).sourceForm() + ", p. " +
           QString::number(pc.displayPage()) + "\n";
    out += pc.textChunks.join("\n[…]\n") + "\n";
  }
  return out;
}

std::expected<QString, QString>
McpLibrary::listPapers(const QJsonObject &args) {
  if (auto r = openActiveProject(); !r)
    return std::unexpected(r.error());
  const QString filter = args.value("filter").toString().trimmed();
  const LibraryRepository repo(m_db);

  struct Line {
    QString sortKey;
    QString text;
  };
  QList<Line> lines;
  QMap<QString, int> pagesByHash;
  for (const IndexedPaper &p : repo.indexedPapers())
    pagesByHash.insert(p.paperHash, p.pages);

  // Ingestion already adds a Summaries row (file name as title): a paper is
  // summarized once its summary JSON is stored.
  const QHash<QString, ZoteroMeta> zotero = zoteroMeta();
  QSet<QString> summarized;
  for (SummaryRow s : repo.summaries()) {
    if (s.summaryJson.toByteArray().isEmpty())
      continue;
    if (const auto it = zotero.constFind(s.paperHash); it != zotero.constEnd())
      MetadataResolver::applyZotero(s, *it);
    summarized.insert(s.paperHash);
    QString text = CitationFormat::paperMark(s.paperHash) + " " +
                   (s.authors.isEmpty() ? QString() : s.authors + " ") +
                   (s.year.isEmpty() || s.year == "0" ? QString()
                                                      : "(" + s.year + "). ") +
                   CitationFormat::sentence(s.title);
    if (!s.journal.isEmpty())
      text += " " + CitationFormat::sentence(s.journal);
    if (pagesByHash.contains(s.paperHash))
      text += QString(" %1 pages.").arg(pagesByHash.value(s.paperHash));
    text += " Summary: yes.";
    lines.append({s.authors + s.title, text});
  }
  for (const IndexedPaper &p : repo.indexedPapers()) {
    if (summarized.contains(p.paperHash))
      continue;
    PaperLabel l =
        MetadataResolver::paperLabel(p.paperHash, p.cleanTitle, p.citation);
    if (const auto it = zotero.constFind(p.paperHash); it != zotero.constEnd())
      l = MetadataResolver::withZotero(l, *it);
    lines.append({l.sourceForm(),
                  CitationFormat::paperMark(p.paperHash) + " " +
                      l.sourceForm() +
                      QString(". %1 pages. Summary: no (file name shown).")
                          .arg(p.pages)});
  }

  if (!filter.isEmpty()) {
    lines.removeIf([&filter](const Line &l) {
      return !l.text.contains(filter, Qt::CaseInsensitive);
    });
  }
  std::sort(lines.begin(), lines.end(), [](const Line &a, const Line &b) {
    return a.sortKey.compare(b.sortKey, Qt::CaseInsensitive) < 0;
  });
  if (lines.isEmpty())
    return filter.isEmpty()
               ? QString("Project \"%1\" has no papers yet.").arg(m_project)
               : QString("No paper matches \"%1\".").arg(filter);

  QString out = QString("Project \"%1\": %2 paper(s)")
                    .arg(m_project)
                    .arg(lines.size());
  if (!filter.isEmpty())
    out += QString(" matching \"%1\"").arg(filter);
  out += ".\n\n";
  for (const Line &l : lines)
    out += l.text + "\n";
  return out;
}

std::expected<QString, QString>
McpLibrary::getSummary(const QJsonObject &args) {
  if (auto r = openActiveProject(); !r)
    return std::unexpected(r.error());
  const auto hash = resolvePaper(args.value("paper").toString());
  if (!hash)
    return std::unexpected(hash.error());

  for (const SummaryRow &row : LibraryRepository(m_db).summaries()) {
    if (row.paperHash != *hash || row.summaryJson.toByteArray().isEmpty())
      continue;
    auto s = SummaryFormat::tryParseJson(
        QString::fromUtf8(row.summaryJson.toByteArray()));
    if (!s)
      return std::unexpected(
          QStringLiteral("The stored summary of this paper is unreadable."));
    const auto zotero = zoteroMeta();
    if (const auto it = zotero.constFind(*hash); it != zotero.constEnd())
      MetadataResolver::applyZotero(*s, *it);
    QString out = CitationFormat::paperMark(*hash) + " " + s->title + "\n";
    if (!s->authors.isEmpty())
      out += "Authors: " + s->authors.join("; ") + "\n";
    if (s->year > 0)
      out += "Year: " + QString::number(s->year) + "\n";
    if (!s->journal.isEmpty())
      out += "Journal: " + s->journal + "\n";
    if (!s->topics.isEmpty())
      out += "Topics: " + s->topics.join(", ") + "\n";
    out += "\n## Research objective\n" + s->researchObjective + "\n";
    out += "\n## Methodology\n" + s->methodology + "\n";
    out += "\n## Core findings and significance\n" +
           bulletList(s->coreFindingsAndSignificance);
    out += "\n## Critical limitations\n" + bulletList(s->criticalLimitations);
    out += "\n## Citable summary\n" + s->citableSummary + "\n";
    out += "\n(Generated by " + row.llmodel +
           "; check details against the pages with get_page.)\n";
    return out;
  }
  return std::unexpected(
      QString("%1 has no summary yet: read it with get_page or "
              "search_library.")
          .arg(CitationFormat::paperMark(*hash)));
}

std::expected<QString, QString> McpLibrary::getPage(const QJsonObject &args) {
  if (auto r = openActiveProject(); !r)
    return std::unexpected(r.error());
  const auto hash = resolvePaper(args.value("paper").toString());
  if (!hash)
    return std::unexpected(hash.error());

  const QMap<int, QString> pages = SummaryPrompt::pageTexts(
      m_embeddings->getChunksByHash(m_db, *hash));
  if (pages.isEmpty())
    return std::unexpected(QString("%1 has no text in the library.")
                               .arg(CitationFormat::paperMark(*hash)));
  const int lastPage = pages.lastKey() + 1;
  const int page = args.value("page").toInt(0);
  if (page < 1 || page > lastPage)
    return std::unexpected(QString("%1 has %2 page(s): page must be between 1 "
                                   "and %2.")
                               .arg(CitationFormat::paperMark(*hash))
                               .arg(lastPage));
  const QString header = CitationFormat::pageMark(*hash, page) + " " +
                         label(*hash) + ", p. " + QString::number(page) +
                         QString(" (of %1)\n\n").arg(lastPage);
  if (!pages.contains(page - 1))
    return header + "(No text was extracted from this page: it may be blank "
                    "or a figure.)\n";
  return header + pages.value(page - 1) + "\n";
}

std::expected<QString, QString>
McpLibrary::getCitations(const QJsonObject &args) {
  if (auto r = openActiveProject(); !r)
    return std::unexpected(r.error());
  const auto hash = resolvePaper(args.value("paper").toString());
  if (!hash)
    return std::unexpected(hash.error());

  for (const CitationRow &row : LibraryRepository(m_db).citations()) {
    if (row.paperHash != *hash)
      continue;
    auto section = [](const QString &title, const QString &json, int total) {
      const QJsonArray works = QJsonDocument::fromJson(json.toUtf8()).array();
      QString out = QString("\n## %1 (%2 on OpenAlex").arg(title).arg(total);
      if (works.size() < total)
        out += QString(", %1 listed").arg(works.size());
      out += ")\n";
      if (works.isEmpty())
        return out + "None found.\n";
      for (const QJsonValue &w : works)
        out += "- " + CitationFormat::apaReference(w.toObject(), "Untitled.") +
               "\n";
      return out;
    };
    // Citations columns: cited_by = works this paper cites (its references),
    // cites = works that cite it.
    return CitationFormat::paperMark(*hash) + " " + label(*hash) + "\n" +
           section("References (works this paper cites)", row.citedBy,
                   row.citedByCount) +
           section("Cited by (works citing this paper)", row.cites,
                   row.citesCount);
  }
  return std::unexpected(
      QString("%1 has no citation data: OpenAlex data is fetched once a "
              "paper is summarized, and may not be found.")
          .arg(CitationFormat::paperMark(*hash)));
}

/* ── Registration ───────────────────────────────────────────────── */

void McpLibrary::registerTools(McpServer &server) {
  using Fn = std::expected<QString, QString> (McpLibrary::*)(const QJsonObject &);
  auto bind = [this](Fn fn) {
    return [this, fn](const QJsonObject &a) { return (this->*fn)(a); };
  };

  server.addTool(
      {"search_library", "Search the library",
       "Semantic search over the text of every paper in the user's HodeIA "
       "library (the project open in HodeIA). Returns the most relevant "
       "pages, each with its mark [@id:pN], the paper's authors, year and "
       "title, and the matching passages. Cite what you use with the marks.",
       schema({{"query", stringProp("What to look for, in natural language.")},
               {"k", intProp("Number of pages to return (default 8).", 1,
                             kMaxResults)},
               {"papers",
                stringArrayProp("Optional: search only these papers (ids as "
                                "for get_summary). Omit to search the whole "
                                "library.",
                                kMaxPapers)}},
              {"query"}),
       bind(&McpLibrary::searchLibrary)});
  server.addTool(
      {"list_papers", "List the papers",
       "List the papers of the library with their id [@id], authors, year, "
       "title, journal, page count, and whether HodeIA has summarized them.",
       schema({{"filter",
                stringProp("Optional text to keep only the papers whose "
                           "line contains it (author, title, year...).")}}),
       bind(&McpLibrary::listPapers)});
  server.addTool(
      {"get_summary", "Get a paper's summary",
       "The structured summary HodeIA generated for a paper: objective, "
       "methodology, findings, limitations, citable summary.",
       schema({{"paper", stringProp(kPaperIdHelp)}}, {"paper"}),
       bind(&McpLibrary::getSummary)});
  server.addTool(
      {"get_page", "Read a page",
       "The full extracted text of one page of a paper, to read a passage in "
       "context or check a claim.",
       schema({{"paper", stringProp(kPaperIdHelp)},
               {"page", intProp("Page number, from 1.", 1, 100000)}},
              {"paper", "page"}),
       bind(&McpLibrary::getPage)});
  server.addTool(
      {"get_citations", "Get a paper's citations",
       "The works a paper cites and the works citing it, from OpenAlex, as "
       "APA references with DOIs.",
       schema({{"paper", stringProp(kPaperIdHelp)}}, {"paper"}),
       bind(&McpLibrary::getCitations)});
}
