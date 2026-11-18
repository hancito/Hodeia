#pragma once

#include "pagecontext.h"

#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>

/// Citations in a query answer.  The prompt numbers each page context
/// ("=== PAGE CONTEXT [3] — …"), the model cites with those numbers, and
/// the answer gets them replaced by author-year citations plus a Sources
/// footer listing the cited papers and pages.
namespace CitationFormat {

/// Replace the model's reference tags ([3], [2][5], [2, 5]) with compact
/// author-year citations built from `pages`, e.g.
/// "(Walker & Ben-Akiva, 2002, pp. 5, 7; Marley, 2002, p. 2)", so the
/// inline pages always match the Sources footer.  The indices of the cited
/// page contexts are added to `cited`.  Tags that reference no known page
/// context are left untouched; Markdown links "[x](url)" are skipped.
QString resolveReferenceTags(const QString &text,
                             const QList<PageContext> &pages,
                             QSet<int> &cited);

/// The "Sources" section appended to an answer: the papers of the `cited`
/// page contexts (all of them if `cited` is empty, i.e. the model ignored the
/// reference tags), grouped by paper in retrieval order, with their pages.
/// Empty when there are no pages.
QString sourcesSection(const QList<PageContext> &pages,
                       const QSet<int> &cited);

/// "p. 4" or "pp. 2, 5, 7" (sorted, duplicates removed).
QString pageRange(QList<int> pages);

/// APA-style reference of an OpenAlex work as stored in the Citations table
/// ({display_name, authors, publication_year, journal, doi}):
/// "Authors (Year). Title. Journal. [doi](url)".  Authors are stored joined
/// with U+001F, as each name contains a comma ("Family, Given").
/// `untitled` stands for a missing title.
QString apaReference(const QJsonObject &work, const QString &untitled);

/// `text` ending a sentence: a period added unless it already ends with
/// ".", "?" or "!" ("J. Econ." stays, not "J. Econ..").
QString sentence(const QString &text);

/// Characters of the paper hash used as its short identifier.
inline constexpr int ShortIdLength = 12;
/// Short identifier of a paper: the start of its hash.
QString shortId(const QString &paperHash);
/// Stable reference to a paper, "[@95f8ed154398]".
QString paperMark(const QString &paperHash);
/// Stable reference to a page (1-based), "[@95f8ed154398:p12]".
QString pageMark(const QString &paperHash, int displayPage);

} // namespace CitationFormat
