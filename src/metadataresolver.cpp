#include "metadataresolver.h"

#include "libraryrepository.h"
#include "summaryformat.h"

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

PaperLabel paperLabel(const PageContext &pc) {
  const PaperLabel label = paperLabel(pc.paperHash, pc.cleanTitle, pc.citation);
  return pc.zotero ? withZotero(label, *pc.zotero) : label;
}

QString displayName(const QString &zoteroName) {
  const qsizetype comma = zoteroName.indexOf(',');
  if (comma < 0)
    return zoteroName.simplified();
  const QString last = zoteroName.left(comma).simplified();
  const QString first = zoteroName.mid(comma + 1).simplified();
  return first.isEmpty() ? last : first + ' ' + last;
}

QString compactAuthorList(const QStringList &names) {
  QStringList family;
  for (const QString &n : names) {
    const QString f = n.section(',', 0, 0).simplified();
    if (!f.isEmpty())
      family.append(f);
  }
  if (family.size() <= 1)
    return family.value(0);
  if (family.size() == 2)
    return family[0] + " & " + family[1];
  return family.first() + " et al.";
}

PaperLabel withZotero(PaperLabel label, const ZoteroMeta &zotero) {
  if (!zotero.authors.isEmpty())
    label.authors = compactAuthorList(zotero.authors);
  if (!zotero.year.isEmpty())
    label.year = zotero.year;
  if (!zotero.title.trimmed().isEmpty())
    label.title = zotero.title.simplified();
  return label;
}

namespace {

QStringList displayNames(const QStringList &zoteroNames) {
  QStringList out;
  for (const QString &n : zoteroNames)
    out.append(displayName(n));
  return out;
}

} // namespace

void applyZotero(PaperSummary &summary, const ZoteroMeta &zotero) {
  if (!zotero.title.trimmed().isEmpty())
    summary.title = zotero.title.simplified();
  if (!zotero.authors.isEmpty())
    summary.authors = displayNames(zotero.authors);
  if (!zotero.year.isEmpty())
    summary.year = zotero.year.toInt();
  if (!zotero.journal.trimmed().isEmpty())
    summary.journal = zotero.journal.simplified();
}

void applyZotero(SummaryRow &row, const ZoteroMeta &zotero) {
  if (!zotero.title.trimmed().isEmpty())
    row.title = zotero.title.simplified();
  if (!zotero.authors.isEmpty())
    row.authors = displayNames(zotero.authors).join("; "); // as in Summaries
  if (!zotero.year.isEmpty())
    row.year = zotero.year;
  if (!zotero.journal.trimmed().isEmpty())
    row.journal = zotero.journal.simplified();
}

void attachZotero(QList<PageContext> &pages,
                  const QHash<QString, ZoteroMeta> &byHash) {
  for (PageContext &pc : pages) {
    const auto it = byHash.constFind(pc.paperHash);
    if (it != byHash.constEnd())
      pc.zotero = *it;
  }
}

} // namespace MetadataResolver
