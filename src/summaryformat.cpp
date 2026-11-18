#include "summaryformat.h"
#include "textformat.h"

#include <algorithm>

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStack>

namespace {

/* ── Malformed-JSON repairs ──
   Small LLMs produce a predictable set of malformed-JSON patterns. The
   repairs are applied cumulatively (each result retried against the parser)
   so as much of the summary as possible is recovered. */

// Drop trailing / duplicated commas before a closer
// (e.g. `...positions.", }` or `["a","b",]`).
QString dropTrailingCommas(const QString &in) {
  QString out;
  out.reserve(in.length());
  bool inString = false;
  for (int i = 0; i < in.length(); ++i) {
    const QChar c = in[i];
    if (inString) {
      out += c;
      if (c == '\\' && i + 1 < in.length()) {
        ++i;
        out += in[i];
      } else if (c == '"') {
        inString = false;
      }
      continue;
    }
    if (c == '"') {
      inString = true;
      out += c;
      continue;
    }
    if (c == ',') {
      int j = i + 1;
      while (j < in.length() && (in[j] == ',' || in[j].isSpace()))
        ++j;
      if (j < in.length() && (in[j] == '}' || in[j] == ']'))
        continue; // drop the comma run
    }
    out += c;
  }
  return out;
}

// Escape quotes a model left raw inside a string value, e.g. a copied
// formula: `"the trade involves w" = 0."`.  Inside a string, a `"` really
// closes it only when followed (after spaces) by `:` `}` `]`, by `,` then
// another string / closer, or by the end of the text; any other `"` is text.
QString escapeStrayQuotes(const QString &in) {
  auto nextNonSpace = [&in](int from) {
    while (from < in.length() && in[from].isSpace())
      ++from;
    return from;
  };
  auto closesString = [&](int quotePos) {
    const int j = nextNonSpace(quotePos + 1);
    if (j >= in.length())
      return true;
    const QChar c = in[j];
    if (c == ':' || c == '}' || c == ']')
      return true;
    if (c != ',')
      return false;
    const int k = nextNonSpace(j + 1);
    return k >= in.length() || in[k] == '"' || in[k] == '}' || in[k] == ']';
  };

  QString out;
  out.reserve(in.length() + 8);
  bool inString = false;
  for (int i = 0; i < in.length(); ++i) {
    const QChar c = in[i];
    if (!inString) {
      inString = (c == '"');
      out += c;
      continue;
    }
    if (c == '\\' && i + 1 < in.length()) {
      out += c;
      out += in[++i];
    } else if (c == '"') {
      if (closesString(i)) {
        inString = false;
        out += c;
      } else {
        out += QStringLiteral("\\\"");
      }
    } else {
      out += c;
    }
  }
  return out;
}

// A model bailing mid-value leaves `{..."abc` with an unterminated string
// and unclosed `{` / `[`. Walk the structure (string-aware), close the
// dangling string, then append the needed `]` / `}` in the correct order.
QString appendMissingClosers(const QString &in) {
  QString s = in;
  while (!s.isEmpty() && s.at(s.length() - 1).isSpace())
    s.chop(1);
  if (!s.isEmpty() && s.at(s.length() - 1) == ',')
    s.chop(1);

  QStack<QChar> closers;
  bool inString = false;
  for (int i = 0; i < s.length(); ++i) {
    const QChar c = s[i];
    if (inString) {
      if (c == '\\' && i + 1 < s.length())
        ++i;
      else if (c == '"')
        inString = false;
      continue;
    }
    if (c == '"')
      inString = true;
    else if (c == '{')
      closers.push('}');
    else if (c == '[')
      closers.push(']');
    else if ((c == '}' || c == ']') && !closers.isEmpty())
      closers.pop();
  }
  if (inString)
    s += QLatin1Char('"');
  while (!closers.isEmpty())
    s += closers.pop();
  return s;
}

// Keep only the JSON object: strip Markdown code fences (```json … ```) and
// any reasoning text before the first '{' / after the last '}'.
QString extractJsonObject(const QString &raw) {
  QString text = raw;
  const int firstFence = text.indexOf("```");
  if (firstFence >= 0) {
    text = text.mid(firstFence + 3);
    const int nl = text.indexOf('\n');
    if (nl >= 0 && nl < 10) // skip the fence's language tag
      text = text.mid(nl + 1);
    const int lastFence = text.lastIndexOf("```");
    if (lastFence > 0)
      text.truncate(lastFence);
  }
  const int bracePos = text.indexOf('{');
  if (bracePos < 0)
    return QString();
  text = text.mid(bracePos);
  const int endBrace = text.lastIndexOf('}');
  if (endBrace > 0)
    text.truncate(endBrace + 1);
  return text.trimmed();
}

// Escape a text field so LLM free-text (which may contain <, >, &) is treated
// as literal content, not markup.
QString escapeHtml(const QString &text) {
  QString out = text;
  out.replace(QStringLiteral("&"), QStringLiteral("&amp;"));
  out.replace(QStringLiteral("<"), QStringLiteral("&lt;"));
  out.replace(QStringLiteral(">"), QStringLiteral("&gt;"));
  out.replace(QStringLiteral("\""), QStringLiteral("&quot;"));
  return out;
}

// Build a <ul> of escaped items; empty list yields an empty string.
QString toHtmlList(const QStringList &items) {
  if (items.isEmpty())
    return QString();
  QStringList li;
  for (const QString &item : items)
    li.append(QStringLiteral("<li>%1</li>").arg(escapeHtml(item)));
  return QStringLiteral("<ul>%1</ul>").arg(li.join(QString()));
}

} // namespace

namespace SummaryFormat {

std::optional<PaperSummary> tryParseJson(const QString &raw) {
  PaperSummary summary;
  const QString text = extractJsonObject(raw);
  if (text.isEmpty()) {
    qWarning() << "[SummaryFormat] No JSON object found (length:"
               << raw.length() << ")";
    return std::nullopt;
  }

  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
  if (err.error != QJsonParseError::NoError) {
    const QString origError = err.errorString();
    QString candidate = dropTrailingCommas(text);
    const char *repair = "trailing-comma";
    doc = QJsonDocument::fromJson(candidate.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
      candidate = escapeStrayQuotes(candidate);
      repair = "stray-quotes";
      doc = QJsonDocument::fromJson(candidate.toUtf8(), &err);
    }
    if (err.error != QJsonParseError::NoError) {
      candidate = appendMissingClosers(candidate);
      repair = "missing-closers";
      doc = QJsonDocument::fromJson(candidate.toUtf8(), &err);
    }
    if (err.error != QJsonParseError::NoError) {
      qWarning() << "[SummaryFormat] JSON parse error (unrecovered):"
                 << origError << "| offset:" << err.offset << "| context:"
                 << candidate.mid(qMax(0, err.offset - 30), 60)
                        .replace('\n', " ");
      return std::nullopt;
    }
    qInfo() << "[SummaryFormat] Repaired malformed summary JSON via" << repair;
  }

  // Models often emit LaTeX despite the prompt; convert it to Unicode here so
  // every consumer (UI, PDF export) and already-stored summaries benefit.
  auto clean = [](const QJsonValue &v) {
    return TextFormat::latexToUnicode(v.toString(),
                                      TextFormat::Target::PlainText);
  };

  const QJsonObject obj = doc.object();
  static const QStringList kFields = {
      "title",       "authors",          "journal",
      "year",        "topics",           "researchObjective",
      "methodology", "coreFindingsAndSignificance",
      "criticalLimitations",             "citableSummary"};
  if (std::none_of(kFields.cbegin(), kFields.cend(),
                   [&obj](const QString &k) { return obj.contains(k); })) {
    qWarning() << "[SummaryFormat] JSON object has none of the summary fields";
    return std::nullopt;
  }
  summary.title = clean(obj.value("title"));
  summary.journal = obj.value("journal").toString();
  summary.year = obj.value("year").toInt();
  summary.researchObjective = clean(obj.value("researchObjective"));
  summary.methodology = clean(obj.value("methodology"));
  for (const auto &f : obj.value("coreFindingsAndSignificance").toArray())
    summary.coreFindingsAndSignificance.append(clean(f));
  for (const auto &l : obj.value("criticalLimitations").toArray())
    summary.criticalLimitations.append(clean(l));
  summary.citableSummary = clean(obj.value("citableSummary"));
  for (const auto &a : obj.value("authors").toArray())
    summary.authors.append(a.toString());
  for (const auto &t : obj.value("topics").toArray())
    summary.topics.append(t.toString());
  return summary;
}

PaperSummary parseJson(const QString &raw) {
  return tryParseJson(raw).value_or(PaperSummary{});
}

QString toHtml(const PaperSummary &s) {
  QString title = s.title;
  if (!title.isEmpty())
    title[0] = title[0].toUpper();
  const QString authors = s.authors.join(QStringLiteral(", "));
  const QString topics = s.topics.join(QStringLiteral(", "));

  QString html;
  html += QStringLiteral("<h2>%1</h2>").arg(escapeHtml(title));
  html += QStringLiteral("<h4>%1 (%2)</h4>")
              .arg(escapeHtml(authors), QString::number(s.year));
  html += QStringLiteral("<h4><em>Journal: %1</em></h4>")
              .arg(escapeHtml(s.journal));
  if (!topics.isEmpty())
    html +=
        QStringLiteral("<h4><em>Topics: %1</em></h4>").arg(escapeHtml(topics));
  html += QStringLiteral("<h3>Research objective</h3>");
  html += QStringLiteral("<p>%1</p>").arg(escapeHtml(s.researchObjective));
  html += QStringLiteral("<h3>Methodology</h3>");
  html += QStringLiteral("<p>%1</p>").arg(escapeHtml(s.methodology));
  html += QStringLiteral("<h3>Core findings and significance</h3>");
  html += toHtmlList(s.coreFindingsAndSignificance);
  html += QStringLiteral("<h3>Critical Limitations</h3>");
  html += toHtmlList(s.criticalLimitations);
  html += QStringLiteral("<h3>Citable summary</h3>");
  html += QStringLiteral("<p>%1</p>").arg(escapeHtml(s.citableSummary));
  return html;
}

QString reportToHtml(const QList<PaperSummary> &papers) {
  QString html;
  if (papers.size() > 1)
    html += QStringLiteral(
        "<h1 class=\"center\">Research Summary Report</h1><hr>");
  QStringList sections;
  for (const PaperSummary &paper : papers)
    sections.append(toHtml(paper));
  return html + sections.join(QStringLiteral("<hr>"));
}

} // namespace SummaryFormat
