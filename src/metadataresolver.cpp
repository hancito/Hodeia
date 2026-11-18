#include "metadataresolver.h"

#include <QRegularExpression>
#include <QStringList>

QString PaperLabel::inlineForm() const {
  return year.isEmpty() ? authors : authors + ", " + year;
}

QString PaperLabel::footerForm() const {
  return year.isEmpty() ? authors : authors + " (" + year + ")";
}

QString PaperLabel::sourceForm() const {
  return title.isEmpty() ? footerForm() : footerForm() + " — " + title;
}

namespace {

QString elide(const QString &s, int max) {
  return s.size() <= max ? s : s.left(max - 1).trimmed() + QStringLiteral("…");
}

} // namespace

namespace MetadataResolver {

QString compactAuthors(const QString &authors) {
  QStringList names;
  for (const QString &a : authors.split(QRegularExpression("\\s*[,;]\\s*"),
                                        Qt::SkipEmptyParts)) {
    const QStringList words = a.simplified().split(' ');
    names.append(words.last());
  }
  if (names.isEmpty())
    return authors.trimmed();
  if (names.size() == 1)
    return names.first();
  if (names.size() == 2)
    return names[0] + " & " + names[1];
  return names.first() + " et al.";
}

PaperLabel paperLabel(const QString &paperHash, const QString &cleanTitle,
                      const QString &citation) {
  static const QRegularExpression authorYear(
      QStringLiteral("^(.+?)\\s*\\((\\d{4}|0)\\)$"));
  static const QRegularExpression zotero(
      QStringLiteral("^(.+?)\\s+-\\s+(\\d{4})\\s+-\\s+(.+)$"));
  static const QRegularExpression zoteroNoYear(
      QStringLiteral("^(.{2,60}?)\\s+-\\s+(.+)$"));

  const QString title = cleanTitle.simplified();
  const QString cite = citation.simplified();

  if (!cite.isEmpty() && cite != title) {
    const auto m = authorYear.match(cite);
    if (m.hasMatch()) {
      const QString year = m.captured(2) == "0" ? QString() : m.captured(2);
      return {compactAuthors(m.captured(1)), year, title};
    }
    return {elide(cite, 60), QString(), title};
  }
  if (title.isEmpty())
    return {paperHash.left(12) + "…", QString(), QString()};
  if (const auto m = zotero.match(title); m.hasMatch())
    return {m.captured(1), m.captured(2), m.captured(3)};
  if (const auto m = zoteroNoYear.match(title); m.hasMatch())
    return {m.captured(1), QString(), m.captured(2)};
  // A bare file name such as "Zanardelli et al": short enough to stand alone.
  return {elide(title, 50), QString(), title.size() > 50 ? title : QString()};
}

} // namespace MetadataResolver
