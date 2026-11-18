#pragma once

#include "pagecontext.h"

#include <QString>

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

/// How a paper is named wherever it is cited (prompt, answer, sources).
/// Today from the chunk metadata (summary, else file name); later Zotero
/// metadata will take precedence here (zoteroPriority).
namespace MetadataResolver {

/// Label of a paper.  Summarized papers carry "Authors (year)" in
/// `citation`; unsummarized ones only have their file name as title and
/// citation, often in the Zotero form "Authors - Year - Title".  Without
/// either, the start of the hash.
PaperLabel paperLabel(const QString &paperHash, const QString &cleanTitle,
                      const QString &citation);

/// Label of the paper a retrieved page belongs to.
inline PaperLabel paperLabel(const PageContext &pc) {
  return paperLabel(pc.paperHash, pc.cleanTitle, pc.citation);
}

/// Compact an author list "A. Walker, M. Ben-Akiva, C. Marley" into
/// "Walker et al." (or "Walker & Ben-Akiva" for two authors).
QString compactAuthors(const QString &authors);

} // namespace MetadataResolver
