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

#include <QDate>
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
// Answers are kept to ~10-15k characters: Claude Code shows a longer tool
// result to the model as a short preview only.  An APA reference is ~350
// characters, a paper line of list_papers ~120.
constexpr int kDefaultCitations = 15; // per list, per call
constexpr int kMaxCitations = 50;
constexpr int kDefaultPapers = 100; // list_papers lines per call
constexpr int kMaxListedPapers = 200;
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
  // Every paper gets its Summaries row at import.  A paper with chunks only
  // (none known today) is looked for in the chunks, which is much slower.
  QStringList matches = repo.paperHashesStartingWith(prefix);
  if (matches.isEmpty())
    for (const IndexedPaper &p : repo.indexedPapers())
      if (p.paperHash.startsWith(prefix))
        matches.append(p.paperHash);
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

std::optional<ZoteroMeta> McpLibrary::zoteroMeta(const QString &paperHash) {
  if (!m_zoteroPriority)
    return std::nullopt;
  const auto item = ZoteroRepository(m_db).itemByHash(paperHash);
  return item ? std::optional(item->meta) : std::nullopt;
}

QString McpLibrary::label(const QString &paperHash) {
  const LibraryRepository repo(m_db);
  if (const auto p = repo.indexedPaper(paperHash))
    return label(paperHash, p->cleanTitle, p->citation);
  return label(paperHash, repo.summaryTitle(paperHash), QString());
}

QString McpLibrary::label(const QString &paperHash, const QString &cleanTitle,
                          const QString &citation) {
  PaperLabel l = MetadataResolver::paperLabel(paperHash, cleanTitle, citation);
  if (const auto zotero = zoteroMeta(paperHash))
    l = MetadataResolver::withZotero(l, *zotero);
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
  // One pass over the chunks (the slow read): pages, and the label of the
  // papers not summarized.
  const QList<IndexedPaper> indexed = repo.indexedPapers();
  QMap<QString, int> pagesByHash;
  for (const IndexedPaper &p : indexed)
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
  for (const IndexedPaper &p : indexed) {
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
  // A page of the list: a large library is read in several calls.
  const int offset = qMax(0, args.value("offset").toInt(0));
  const int limit =
      std::clamp(args.value("limit").toInt(kDefaultPapers), 1, kMaxListedPapers);
  if (offset >= lines.size())
    return out + QString(". No more: offset %1 is past the end.\n").arg(offset);
  const qsizetype end = qMin(lines.size(), qsizetype(offset) + limit);
  if (offset > 0 || end < lines.size())
    out += QString(", %1–%2 below").arg(offset + 1).arg(end);
  out += ".\n\n";
  for (qsizetype i = offset; i < end; ++i)
    out += lines[i].text + "\n";
  if (end < lines.size())
    out += QString("\n(More: call list_papers again with offset %1%2.)\n")
               .arg(end)
               .arg(filter.isEmpty() ? QString()
                                     : QString(" and the same filter"));
  return out;
}

std::expected<QString, QString>
McpLibrary::getSummary(const QJsonObject &args) {
  if (auto r = openActiveProject(); !r)
    return std::unexpected(r.error());
  const auto hash = resolvePaper(args.value("paper").toString());
  if (!hash)
    return std::unexpected(hash.error());

  if (const auto row = LibraryRepository(m_db).summary(*hash);
      row && !row->summaryJson.toByteArray().isEmpty()) {
    auto s = SummaryFormat::tryParseJson(
        QString::fromUtf8(row->summaryJson.toByteArray()));
    if (!s)
      return std::unexpected(
          QStringLiteral("The stored summary of this paper is unreadable."));
    if (const auto zotero = zoteroMeta(*hash))
      MetadataResolver::applyZotero(*s, *zotero);
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
    out += "\n(Generated by " + row->llmodel +
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

  const QList<ChunkSearchResult> chunks = LibraryRepository(m_db).chunks(*hash);
  const QMap<int, QString> pages = SummaryPrompt::pageTexts(chunks);
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
  // The paper's title and citation are on each of its chunks.
  const QString header = CitationFormat::pageMark(*hash, page) + " " +
                         label(*hash, chunks.first().cleanTitle,
                               chunks.first().citation) +
                         ", p. " + QString::number(page) +
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

  const QString list = args.value("list").toString("both");
  if (list != "both" && list != "references" && list != "cited_by")
    return std::unexpected(
        QStringLiteral("list must be \"references\", \"cited_by\" or "
                       "\"both\"."));
  const int offset = qMax(0, args.value("offset").toInt(0));
  const int limit =
      std::clamp(args.value("limit").toInt(kDefaultCitations), 1, kMaxCitations);

  if (const auto row = LibraryRepository(m_db).citation(*hash)) {
    // One page of each list asked for: long lists are read in several calls.
    auto section = [&](const QString &list, const QString &title,
                       const QString &json, int total) {
      const QJsonArray works = QJsonDocument::fromJson(json.toUtf8()).array();
      QString out = QString("\n## %1 (%2 on OpenAlex").arg(title).arg(total);
      if (works.size() < total)
        out += QString(", %1 listed").arg(works.size());
      out += ")\n";
      if (works.isEmpty())
        return out + "None found.\n";
      if (offset >= works.size())
        return out + QString("No more: the list has %1 works.\n")
                         .arg(works.size());
      const qsizetype end = qMin(works.size(), qsizetype(offset) + limit);
      if (offset > 0 || end < works.size())
        out += QString("Works %1–%2 of %3:\n")
                   .arg(offset + 1)
                   .arg(end)
                   .arg(works.size());
      for (qsizetype i = offset; i < end; ++i)
        out += "- " +
               CitationFormat::apaReference(works.at(i).toObject(),
                                            "Untitled.") +
               "\n";
      if (end < works.size())
        out += QString("(More: call get_citations again with list \"%1\" "
                       "and offset %2.)\n")
                   .arg(list)
                   .arg(end);
      return out;
    };
    // Citations columns: cited_by = works this paper cites (its references),
    // cites = works that cite it.
    QString out = CitationFormat::paperMark(*hash) + " " + label(*hash) + "\n";
    if (list != "cited_by")
      out += section("references", "References (works this paper cites)",
                     row->citedBy, row->citedByCount);
    if (list != "references")
      out += section("cited_by", "Cited by (works citing this paper)",
                     row->cites, row->citesCount);
    return out;
  }
  return std::unexpected(
      QString("%1 has no citation data: OpenAlex data is fetched once a "
              "paper is summarized, and may not be found.")
          .arg(CitationFormat::paperMark(*hash)));
}

/* ── Writes ─────────────────────────────────────────────────────── */

namespace {

/// The summary fields the agent must fill (HodeIA's summary schema, as the
/// Summarizer's prompt asks for it); journal and topics may be unknown.
const QStringList kRequiredTextFields = {"title", "researchObjective",
                                         "methodology", "citableSummary"};
const QStringList kRequiredListFields = {"authors",
                                         "coreFindingsAndSignificance",
                                         "criticalLimitations"};

QString missingFields(const QJsonObject &s) {
  QStringList missing;
  for (const QString &f : kRequiredTextFields)
    if (s.value(f).toString().trimmed().isEmpty())
      missing << f;
  for (const QString &f : kRequiredListFields) {
    const QJsonArray a = s.value(f).toArray();
    if (std::none_of(a.cbegin(), a.cend(), [](const QJsonValue &v) {
          return !v.toString().trimmed().isEmpty();
        }))
      missing << f;
  }
  if (!s.value("year").isDouble())
    missing << "year";
  return missing.join(", ");
}

} // namespace

std::expected<QString, QString>
McpLibrary::saveSummary(const QJsonObject &args) {
  if (auto r = openActiveProject(); !r)
    return std::unexpected(r.error());
  const auto hash = resolvePaper(args.value("paper").toString());
  if (!hash)
    return std::unexpected(hash.error());

  const QJsonObject summaryArg = args.value("summary").toObject();
  if (const QString missing = missingFields(summaryArg); !missing.isEmpty())
    return std::unexpected(
        QString("The summary is incomplete: %1 must be given (see the "
                "tool's schema). Nothing was saved.")
            .arg(missing));
  const int year = summaryArg.value("year").toInt();
  if (year < 0 || year > QDate::currentDate().year() + 1)
    return std::unexpected(
        QString("year %1 is not a publication year (0 if unknown). Nothing "
                "was saved.")
            .arg(year));
  // Only the summary fields, stored as the Summarizer stores a model's JSON.
  QJsonObject fields;
  for (const QString &f :
       {"title", "authors", "journal", "year", "topics", "researchObjective",
        "methodology", "coreFindingsAndSignificance", "criticalLimitations",
        "citableSummary"})
    if (summaryArg.contains(f))
      fields.insert(f, summaryArg.value(f));
  const QString rawJson =
      QString::fromUtf8(QJsonDocument(fields).toJson(QJsonDocument::Compact));
  const auto parsed = SummaryFormat::tryParseJson(rawJson);
  if (!parsed)
    return std::unexpected(
        QStringLiteral("The summary could not be read. Nothing was saved."));

  LibraryRepository repo(m_db);
  const auto existing = repo.summary(*hash);
  const bool hadSummary =
      existing && !existing->summaryJson.toByteArray().isEmpty();
  if (hadSummary && !args.value("replace").toBool(false))
    return std::unexpected(
        QString("%1 already has a summary (by %2). Nothing was saved. Pass "
                "\"replace\": true only if the user asked to redo it.")
            .arg(CitationFormat::paperMark(*hash), existing->llmodel));

  QString model = args.value("model").toString().simplified();
  if (model.isEmpty())
    model = QStringLiteral("MCP agent");
  if (auto r = repo.saveSummary(*hash, *parsed, rawJson, model); !r)
    return std::unexpected(
        QString("The summary could not be saved (HodeIA may be writing to "
                "the library, e.g. during an import: try again shortly): %1")
            .arg(r.error()));
  qInfo() << "[MCP] Summary saved for" << hash->left(12) << "by" << model
          << (hadSummary ? "(replaced)" : "");
  return QString("Summary of %1 %2 (%3). HodeIA shows it in its paper list "
                 "and will look the paper up on OpenAlex for its citations.")
      .arg(CitationFormat::paperMark(*hash),
           hadSummary ? QStringLiteral("replaced") : QStringLiteral("saved"),
           parsed->title);
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
       "title, journal, page count, and whether HodeIA has summarized them. "
       "A large library comes a page at a time: the answer says how to get "
       "the next one.",
       schema({{"filter",
                stringProp("Optional text to keep only the papers whose "
                           "line contains it (author, title, year...).")},
               {"offset", intProp("Skip this many papers (default 0), to "
                                  "read the next page.",
                                  0, 100000)},
               {"limit", intProp(QString("Papers per call (default %1).")
                                     .arg(kDefaultPapers),
                                 1, kMaxListedPapers)}}),
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
  {
    auto text = [](const QString &d) { return stringProp(d); };
    auto list = [](const QString &d) {
      return QJsonObject{{"type", "array"},
                         {"description", d},
                         {"items", QJsonObject{{"type", "string"}}}};
    };
    const QJsonObject summarySchema = schema(
        {{"title", text("The full title of the paper.")},
         {"authors", list("The authors' names, in the paper's order.")},
         {"journal", text("Journal or conference name (empty if unknown).")},
         {"year", QJsonObject{{"type", "integer"},
                              {"description",
                               "The publication year (0 if unknown)."}}},
         {"topics", list("The 5 most relevant topics or keywords.")},
         {"researchObjective",
          text("The primary question or hypothesis, in 1 sentence.")},
         {"methodology", text("The study design, sample size and key "
                              "variables, in a paragraph.")},
         {"coreFindingsAndSignificance",
          list("The most statistically or theoretically significant "
               "results, one per item.")},
         {"criticalLimitations",
          list("Internal or external validity threats mentioned or "
               "observed, one per item.")},
         {"citableSummary",
          text("A two-sentence summary suitable for a literature review.")}},
        {"title", "authors", "year", "researchObjective", "methodology",
         "coreFindingsAndSignificance", "criticalLimitations",
         "citableSummary"});
    server.addTool(
        {"save_summary", "Save a paper's summary",
         "Store the structured summary you wrote for a paper of the library, "
         "as HodeIA's own summaries: HodeIA lists it, shows it, exports it, "
         "and looks the paper up on OpenAlex. Read the whole paper first "
         "(get_page for each page). Write in English, with precision and "
         "detail, from the paper only; no Markdown, no LaTeX: write formulas "
         "in plain Unicode (α, x², ≤, ∑, a/b). A paper that already has a "
         "summary keeps it unless replace is true.",
         schema({{"paper", stringProp(kPaperIdHelp)},
                 {"summary", summarySchema},
                 {"model", stringProp("Your model's name, shown as the "
                                      "summary's author in HodeIA.")},
                 {"replace",
                  QJsonObject{{"type", "boolean"},
                              {"description",
                               "Replace an existing summary. Only when the "
                               "user asked to redo it (default false)."}}}},
                {"paper", "summary"}),
         bind(&McpLibrary::saveSummary)});
  }
  server.addTool(
      {"get_citations", "Get a paper's citations",
       "The works a paper cites (references) and the works citing it "
       "(cited_by), from OpenAlex, as APA references with DOIs. Long lists "
       "come a page at a time: the answer says how to get the next one.",
       schema({{"paper", stringProp(kPaperIdHelp)},
               {"list", QJsonObject{{"type", "string"},
                                    {"enum", QJsonArray{"both", "references",
                                                        "cited_by"}},
                                    {"description",
                                     "Which list: \"references\", "
                                     "\"cited_by\" or \"both\" (default)."}}},
               {"offset", intProp("Skip this many works of each list "
                                  "(default 0), to read the next page.",
                                  0, 100000)},
               {"limit", intProp(QString("Works per list (default %1).")
                                     .arg(kDefaultCitations),
                                 1, kMaxCitations)}},
              {"paper"}),
       bind(&McpLibrary::getCitations)});
}
