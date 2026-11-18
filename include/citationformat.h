#pragma once

#include "pagecontext.h"

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

} // namespace CitationFormat
