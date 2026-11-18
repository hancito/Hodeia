#include "citationformat.h"

#include "metadataresolver.h"
#include "textformat.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>

namespace CitationFormat {

QString pageRange(QList<int> pages) {
  std::sort(pages.begin(), pages.end());
  pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
  QStringList nums;
  for (int p : pages)
    nums.append(QString::number(p));
  return (pages.size() > 1 ? "pp. " : "p. ") + nums.join(", ");
}

QString sentence(const QString &text) {
  const QString t = text.trimmed();
  if (t.isEmpty() || t.endsWith('.') || t.endsWith('?') || t.endsWith('!'))
    return t;
  return t + ".";
}

QString apaReference(const QJsonObject &work, const QString &untitled) {
  QStringList names;
  for (const QString &s : work.value("authors").toString().split(QChar(0x1F))) {
    const QString t = s.trimmed();
    if (!t.isEmpty())
      names << t;
  }
  QString line;
  if (!names.isEmpty())
    line += (names.size() == 1
                 ? names.first()
                 : names.mid(0, names.size() - 1).join(", ") + " & " +
                       names.last()) +
            " ";
  const int year = work.value("publication_year").toInt();
  if (year > 0)
    line += "(" + QString::number(year) + "). ";
  const QString title = work.value("display_name").toString();
  line += title.isEmpty() ? untitled : sentence(title);
  const QString journal = work.value("journal").toString();
  if (!journal.isEmpty())
    line += " " + sentence(journal);
  const QString doi = work.value("doi").toString();
  if (!doi.isEmpty()) {
    // OpenAlex returns a full URL; a bare DOI gets the canonical resolver.
    const QString url = doi.startsWith("http://", Qt::CaseInsensitive) ||
                                doi.startsWith("https://", Qt::CaseInsensitive)
                            ? doi
                            : QStringLiteral("https://doi.org/") + doi;
    line += " [" + doi + "](" + url + ")";
  }
  return line;
}

QString shortId(const QString &paperHash) {
  return paperHash.left(ShortIdLength);
}

QString paperMark(const QString &paperHash) {
  return "[@" + shortId(paperHash) + "]";
}

QString pageMark(const QString &paperHash, int displayPage) {
  return "[@" + shortId(paperHash) + ":p" + QString::number(displayPage) + "]";
}

QString resolveReferenceTags(const QString &text,
                             const QList<PageContext> &pages,
                             QSet<int> &cited) {
  static const QRegularExpression tags(QStringLiteral(
      "(?:\\[\\s*\\d+(?:\\s*[,;]\\s*\\d+)*\\s*\\])+(?!\\()"));
  static const QRegularExpression number(QStringLiteral("\\d+"));

  QString out;
  qsizetype last = 0;
  auto it = tags.globalMatch(text);
  while (it.hasNext()) {
    const auto mt = it.next();
    out += text.mid(last, mt.capturedStart() - last);
    last = mt.capturedEnd();

    // Group the referenced pages by paper, in citation order.
    QStringList order;
    QHash<QString, QList<int>> pagesByLabel;
    auto nums = number.globalMatch(mt.captured(0));
    while (nums.hasNext()) {
      const int idx = nums.next().captured(0).toInt() - 1;
      if (idx < 0 || idx >= pages.size())
        continue;
      cited.insert(idx);
      const QString label =
          MetadataResolver::paperLabel(pages[idx]).inlineForm();
      if (!pagesByLabel.contains(label))
        order.append(label);
      pagesByLabel[label].append(pages[idx].displayPage());
    }
    if (order.isEmpty()) {
      out += mt.captured(0);
      continue;
    }

    QStringList parts;
    for (const QString &label : order)
      parts.append(TextFormat::escapeMarkdown(label) + ", " +
                   pageRange(pagesByLabel.value(label)));
    // Keep a space before the citation even if the model glued the tag to
    // the previous word ("absence[3]").
    if (!out.isEmpty() && !out.back().isSpace() && out.back() != '(')
      out += ' ';
    out += "(" + parts.join("; ") + ")";
  }
  out += text.mid(last);
  return out;
}

QString sourcesSection(const QList<PageContext> &pages,
                       const QSet<int> &cited) {
  QStringList order;
  QHash<QString, QList<int>> pagesByPaper;
  QHash<QString, PaperLabel> labels;
  for (int i = 0; i < pages.size(); ++i) {
    if (!cited.isEmpty() && !cited.contains(i))
      continue;
    const PageContext &pc = pages[i];
    if (!pagesByPaper.contains(pc.paperHash)) {
      order.append(pc.paperHash);
      labels.insert(pc.paperHash, MetadataResolver::paperLabel(pc));
    }
    pagesByPaper[pc.paperHash].append(pc.displayPage());
  }
  if (order.isEmpty())
    return QString();

  QStringList lines;
  for (const QString &hash : order) {
    const PaperLabel &l = labels[hash];
    QString line = "- **" + TextFormat::escapeMarkdown(l.footerForm()) + "**";
    if (!l.title.isEmpty())
      line += " — *" + TextFormat::escapeMarkdown(l.title) + "*";
    line += " — " + pageRange(pagesByPaper.value(hash));
    lines.append(line);
  }
  // The label is deliberately not translated: the answer is persisted, and
  // "Sources" reads the same in English and French, so stored history stays
  // consistent across UI languages.
  return "\n\n---\n\n**Sources**\n\n" + lines.join("\n");
}

} // namespace CitationFormat
