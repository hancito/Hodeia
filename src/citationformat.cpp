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
