#pragma once

#include "pagecontext.h"
#include "zoterosource.h"

#include <QHash>
#include <QList>
#include <QString>

struct PaperSummary;
struct SummaryRow;

/// Short author-year label and full title of a paper, for citations.
struct PaperLabel {
  QString authors; ///< e.g. "Walker & Ben-Akiva", or a fallback label
  QString year;    ///< empty when unknown
  QString title;   ///< full title, empty when unknown

  /// "Walker & Ben-Akiva, 2002" — inside an inline citation.
  QString inlineForm() const;
  /// "Walker & Ben-Akiva (2002)" — in the Sources footer.
  QString footerForm() const;
  /// "Walker & Ben-Akiva (2002) — Title", or the footer form without a
  /// title: identifies a source in a prompt.
  QString sourceForm() const;
};

/// How a paper is named wherever it is cited or listed (prompt, answer,
/// sources, summary, report, MCP).  From the summary, else the file name;
/// with zoteroPriority, Zotero's title, authors, year and journal take
/// precedence, field by field, where Zotero has a value.  The content of a
/// summary (objective, method, findings...) always comes from the summary.
namespace MetadataResolver {

/// Label of a paper.  Summarized papers carry "Authors (year)" in
/// `citation`; unsummarized ones only have their file name as title and
/// citation, often in the Zotero form "Authors - Year - Title".  Without
/// either, the start of the hash.
PaperLabel paperLabel(const QString &paperHash, const QString &cleanTitle,
                      const QString &citation);

/// Label of the paper a retrieved page belongs to (Zotero's metadata first
/// when attached to the page).
PaperLabel paperLabel(const PageContext &pc);

/* ── Zotero's metadata first (zoteroPriority) ── */

/// `label` with Zotero's title, authors and year where Zotero has them.
PaperLabel withZotero(PaperLabel label, const ZoteroMeta &zotero);
/// Zotero's title, authors, year and journal where Zotero has them.
void applyZotero(PaperSummary &summary, const ZoteroMeta &zotero);
void applyZotero(SummaryRow &row, const ZoteroMeta &zotero);
/// Give each page the Zotero metadata of its paper, if any.
void attachZotero(QList<PageContext> &pages,
                  const QHash<QString, ZoteroMeta> &byHash);

/// "Walker & Ben-Akiva", "Walker et al." from Zotero names ("Walker, Joan";
/// a name without a comma, an institution, is kept whole).
QString compactAuthorList(const QStringList &names);
/// "Joan Walker" from "Walker, Joan"; a name without a comma as it is.
QString displayName(const QString &zoteroName);

/// Compact an author list "A. Walker, M. Ben-Akiva, C. Marley" into
/// "Walker et al." (or "Walker & Ben-Akiva" for two authors).
QString compactAuthors(const QString &authors);

} // namespace MetadataResolver
