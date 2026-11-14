#include "helpers.h"
#include "configmanager.h"
#include "textformat.h"

#include <QAtomicInt>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStack>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include <cstring>
#include <map>
#include <vector>

/* ── Constructor / Destructor ────────────────────────────────── */

Helpers::Helpers(ConfigManager *config, QObject *parent)
    : QObject(parent), m_config(config) {
  m_networkManager = new QNetworkAccessManager(this);

  QObject::connect(m_config, &ConfigManager::activeBackendChanged, this,
                   &Helpers::loadActiveBackendModels);
  QObject::connect(m_config, &ConfigManager::modelCopied, this,
                   &Helpers::loadActiveBackendModels);

  QObject::connect(this, &Helpers::modelCopyProgress, m_config,
                   &ConfigManager::modelCopyProgress);
  QObject::connect(this, &Helpers::modelCopyFinished, m_config,
                   &ConfigManager::modelCopyFinished);
  QObject::connect(this, &Helpers::modelCopied, m_config,
                   &ConfigManager::modelCopied);
  QObject::connect(this, &Helpers::modelCopied, this,
                   &Helpers::refreshLocalModelList, Qt::QueuedConnection);

  refreshLocalModelList();
  loadActiveBackendModels();
}

Helpers::~Helpers() = default;

/* ── Summary parsing ─────────────────────────────────────────── */

PaperSummary Helpers::parseSummaryJson(const QString &raw) {
  PaperSummary summary;
  QString text = raw;

  // Strip markdown code fences (```json ... ``` or ``` ... ```)
  {
    int firstFence = text.indexOf("```");
    if (firstFence >= 0) {
      text = text.mid(firstFence + 3);
      int nl = text.indexOf('\n');
      if (nl >= 0 && nl < 10)
        text = text.mid(nl + 1);
      int lastFence = text.lastIndexOf("```");
      if (lastFence > 0)
        text = text.mid(0, lastFence);
    }
  }

  // Strip reasoning prefix — jump to first '{'
  {
    int bracePos = text.indexOf('{');
    if (bracePos > 0)
      text = text.mid(bracePos);
    int endBrace = text.lastIndexOf('}');
    if (endBrace > 0)
      text = text.mid(0, endBrace + 1);
  }

  text = text.trimmed();

  if (!text.startsWith("{") && !text.startsWith("```")) {
    int bracePos = text.indexOf('{');
    if (bracePos < 0) {
      qWarning() << "[Helpers] No JSON object found (length:" << raw.length()
                 << ")";
      return summary;
    }
    if (bracePos > raw.length() * 0.5)
      qWarning() << "[Helpers] JSON brace at" << bracePos << "of"
                 << raw.length() << "— likely truncated";
  }

  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
  if (err.error != QJsonParseError::NoError) {
    // Small LLMs produce a predictable set of malformed-JSON patterns.
    // Apply a progressive set of repairs (each retried against the parser)
    // so we recover as much of the summary as possible instead of returning
    // an empty object.  Strategy order is cheapest/most-likely first.
    const QJsonParseError::ParseError origError = err.error;

    // ── Repair 1: drop trailing / duplicated commas before a closer ──
    // (e.g. `...positions.", }` or `["a","b",]`)
    auto dropTrailingCommas = [](const QString &in) {
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
    };

    // ── Repair 2: close any unterminated string at the very end ──
    // The model bailing mid-value leaves a dangling `"...` with no closing
    // quote.  If the trailing run of non-quote chars after the last '"' is
    // the tail of an unterminated string, close it.
    auto closeUnterminatedString = [](const QString &in) {
      // Count whether the number of unescaped quotes is odd; if so, the
      // final string is unterminated.
      int quotes = 0;
      for (int i = 0; i < in.length(); ++i) {
        if (in[i] == '\\') {
          ++i;
          continue;
        }
        if (in[i] == '"')
          ++quotes;
      }
      if (quotes % 2 == 1)
        return in + "\"";
      return in;
    };

    // ── Repair 3: append missing closers so the object/array balance ──
    // The model bailing mid-object leaves `{...` with an unclosed `{`/`[`.
    // Walk the structure (string-aware) and append the needed `]`/`}` in
    // the correct order, dropping a trailing comma first.
    auto appendMissingClosers = [](const QString &in) {
      QString s = in;
      // Drop a trailing comma + whitespace that would precede the closers.
      while (!s.isEmpty() && s.at(s.length() - 1).isSpace())
        s.chop(1);
      if (!s.isEmpty() && s.at(s.length() - 1) == ',')
        s.chop(1);

      QStack<QString> openers;
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
        if (c == '"') {
          inString = true;
        } else if (c == '{') {
          openers.push(QStringLiteral("}"));
        } else if (c == '[') {
          openers.push(QStringLiteral("]"));
        } else if (c == '}' || c == ']') {
          if (!openers.isEmpty())
            openers.pop();
        }
      }
      if (inString)
        s += QLatin1Char('"'); // close a dangling string first
      while (!openers.isEmpty())
        s += openers.pop();
      return s;
    };

    QString candidate = text;
    const char *repairName = "none";
    doc = QJsonDocument::fromJson(candidate.toUtf8(), &err);

    if (err.error != QJsonParseError::NoError) {
      candidate = dropTrailingCommas(text);
      repairName = "trailing-comma";
      doc = QJsonDocument::fromJson(candidate.toUtf8(), &err);
    }
    if (err.error != QJsonParseError::NoError) {
      candidate = closeUnterminatedString(dropTrailingCommas(text));
      repairName = "unterminated-string";
      doc = QJsonDocument::fromJson(candidate.toUtf8(), &err);
    }
    if (err.error != QJsonParseError::NoError) {
      candidate = appendMissingClosers(
          closeUnterminatedString(dropTrailingCommas(text)));
      repairName = "missing-closers";
      doc = QJsonDocument::fromJson(candidate.toUtf8(), &err);
    }

    if (err.error != QJsonParseError::NoError) {
      qWarning() << "[Helpers] JSON parse error (unrecovered):"
                 << err.errorString()
                 << "| original error:" << static_cast<int>(origError)
                 << "| offset:" << err.offset << "| context:"
                 << text.mid(qMax(0, err.offset - 30), 60).replace('\n', " ");
      if (text.length() < 200)
        qWarning() << "[Helpers] full cleaned text:" << text;
      return summary;
    }
    if (repairName != QLatin1String("none"))
      qInfo() << "[Helpers] Repaired malformed summary JSON via" << repairName;
  }

  // Models often emit LaTeX despite the prompt; convert it to Unicode here so
  // every consumer (UI, HTML/PDF export) and already-stored summaries benefit.
  auto clean = [](const QJsonValue &v) {
    return TextFormat::latexToUnicode(v.toString(),
                                      TextFormat::Target::PlainText);
  };

  QJsonObject obj = doc.object();
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

/* ── HTML / LaTeX formatting ─────────────────────────────────── */

// Escape a text field so LLM free-text (which may contain <, >, &) is treated
// as literal content, not markup, when building the summary HTML.
static QString escapeHtml(const QString &text) {
  QString out = text;
  out.replace(QStringLiteral("&"), QStringLiteral("&amp;"));
  out.replace(QStringLiteral("<"), QStringLiteral("&lt;"));
  out.replace(QStringLiteral(">"), QStringLiteral("&gt;"));
  out.replace(QStringLiteral("\""), QStringLiteral("&quot;"));
  return out;
}

// Build a <ul> of escaped items; empty list yields an empty string.
static QString toHtmlList(const QStringList &items) {
  if (items.isEmpty())
    return QString();
  QStringList li;
  for (const QString &item : items)
    li.append(QStringLiteral("<li>%1</li>").arg(escapeHtml(item)));
  return QStringLiteral("<ul>%1</ul>").arg(li.join(QString()));
}

QString Helpers::toHtml(const PaperSummary &s) {
  QString authors = s.authors.join(QStringLiteral(", "));
  QString title = s.title;
  if (!title.isEmpty())
    title[0] = title[0].toUpper();
  QString topics = s.topics.join(QStringLiteral(", "));

  // Font family is intentionally NOT set here: the same HTML is rendered both
  // in QML (Google Sans via Fonts.ui) and in the PDF (EB Garamond via
  // kDefaultPdfStyle / default font). Each renderer supplies its own font.
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

QString Helpers::toLatex(const PaperSummary &s) {
  QString authors = s.authors.join(", ");
  QString title = s.title;
  if (!title.isEmpty())
    title[0] = title[0].toUpper();

  QString findings, limitations;
  for (const QString &f : s.coreFindingsAndSignificance)
    findings += QString("\n    \\item %1").arg(f);
  for (const QString &l : s.criticalLimitations)
    limitations += QString("\n    \\item %1").arg(l);

  QStringList parts;
  parts.append(QString("\\section{%1, (%2): %3}")
                   .arg(authors, QString::number(s.year), title));
  parts.append("\\subsection{Research objective}");
  parts.append(s.researchObjective);
  parts.append("");
  parts.append("\\subsection{Methodology}");
  parts.append(s.methodology);
  parts.append("");
  parts.append("\\subsection{Core findings and significance}");
  parts.append(QString("\\begin{itemize}%1\\end{itemize}").arg(findings));
  parts.append("");
  parts.append("\\subsection{Critical Limitations}");
  parts.append(QString("\\begin{itemize}%1\\end{itemize}").arg(limitations));
  parts.append("");
  parts.append("\\subsection{Citable summary}");
  parts.append(s.citableSummary);
  return parts.join("\n");
}

/* ── File copy helper (static, used by thread-pool tasks) ────── */

static bool copyFileToDir(const QString &sourcePath, const QString &destDir,
                          Helpers *helper) {
  QString srcPath = sourcePath;
  if (srcPath.startsWith("file://"))
    srcPath = QUrl(srcPath).toLocalFile();

  QFile src(srcPath);
  if (!src.exists())
    return false;

  QString fileName = QFileInfo(src).fileName();
  QString destPath = destDir + "/" + fileName;
  QDir().mkpath(destDir);

  if (QFile::exists(destPath) && !QFile::remove(destPath))
    return false;
  if (!src.open(QIODevice::ReadOnly))
    return false;

  qint64 total = src.size();
  if (total <= 0) {
    src.close();
    return false;
  }

  QFile dest(destPath);
  if (!dest.open(QIODevice::WriteOnly)) {
    src.close();
    return false;
  }

  // Heap-allocated buffer.  This runs on a QThreadPool worker thread whose
  // default stack is only 1 MB on Windows; a 1 MB *stack* array here overflowed
  // the stack and crashed the process on the first copy.
  const qint64 bufSize = 1024 * 1024;
  const QByteArray buf =
      QByteArray(static_cast<int>(bufSize), Qt::Uninitialized);
  qint64 copied = 0;
  while (!src.atEnd()) {
    const qint64 n = src.read(const_cast<char *>(buf.constData()), bufSize);
    if (n <= 0)
      break;
    copied += dest.write(buf.constData(), n);
    if (helper)
      QMetaObject::invokeMethod(
          helper,
          [helper, fileName, pct = static_cast<int>(copied * 100 / total)] {
            Q_EMIT helper->modelCopyProgress(pct, fileName);
          },
          Qt::QueuedConnection);
  }
  dest.close();
  src.close();

  if (copied != total) {
    QFile::remove(destPath);
    return false;
  }
  if (helper)
    QMetaObject::invokeMethod(
        helper,
        [helper, fileName] { Q_EMIT helper->modelCopyProgress(100, fileName); },
        Qt::QueuedConnection);
  return true;
}

/* ── Summary QML wrappers ────────────────────────────────────── */

QString Helpers::summaryToHtml(const QString &json) const {
  return toHtml(parseSummaryJson(json));
}

QString Helpers::summaryToLatex(const QString &json) const {
  return toLatex(parseSummaryJson(json));
}

/* ── PDF export (MuPDF) ──────────────────────────────────────── */

// PDFs are laid out by MuPDF's HTML/CSS engine (fz_story) rather than by
// QTextDocument/QPdfWriter: the output no longer depends on the platform font
// engine (on Windows, Qt embedded the EB Garamond variable font as a single
// bold face, hinted advances to whole pixels and broke ligature ToUnicode).

// Fonts are resolved by MuPDF from <appdir>/Fonts/EB_Garamond/static via the
// archive passed to fz_new_story(); one @font-face per weight/style.
static const char kPdfCss[] =
    "@font-face { font-family: 'EB Garamond'; "
    "  src: url('EBGaramond-Regular.ttf'); }"
    "@font-face { font-family: 'EB Garamond'; font-style: italic; "
    "  src: url('EBGaramond-Italic.ttf'); }"
    "@font-face { font-family: 'EB Garamond'; font-weight: bold; "
    "  src: url('EBGaramond-Bold.ttf'); }"
    "@font-face { font-family: 'EB Garamond'; font-weight: bold; "
    "  font-style: italic; src: url('EBGaramond-BoldItalic.ttf'); }"
    "body { font-family: 'EB Garamond'; font-size: 11pt; line-height: 1.3; "
    "  color: #000; }"
    "p { margin: 0 0 0.6em 0; text-align: justify; }"
    "ul { margin: 0 0 0.6em 0; padding-left: 1.4em; }"
    "li { margin: 0 0 0.25em 0; text-align: justify; }"
    "h1 { font-size: 22pt; font-weight: bold; margin: 0 0 0.2em 0; }"
    "h2 { font-size: 16pt; font-weight: bold; margin: 0.4em 0 0.3em 0; }"
    "h3 { font-size: 13pt; font-weight: bold; margin: 0.9em 0 0.3em 0; }"
    "h4 { font-size: 11pt; font-weight: bold; margin: 0 0 0.25em 0; }"
    "h5 { font-size: 9pt; font-weight: normal; margin: 0 0 0.5em 0; }"
    "code { font-size: 10pt; }"
    "hr { border: none; border-top: 0.5pt solid #999; margin: 1.2em 0; }"
    ".new-page { page-break-before: always; }"
    ".center { text-align: center; }"
    ".muted { color: #888; font-size: 8pt; }";

// Attribution appended after the content (rule + centered muted line).
static const QString kPdfFooterHtml =
    QStringLiteral("<hr><p class=\"center muted\">Generated with Hodeia</p>");

// A4 in points, and the content area inside the margins.
static constexpr fz_rect kPdfPage = {0.0f, 0.0f, 595.28f, 841.89f};
static constexpr float kPdfMarginX = 62.0f;      // ~22 mm
static constexpr float kPdfMarginTop = 57.0f;    // ~20 mm
static constexpr float kPdfMarginBottom = 68.0f; // ~24 mm (room for page no.)

/* ── Ligature ToUnicode fix-up ──
   MuPDF builds each font's ToUnicode CMap by reversing the font's cmap table,
   so glyphs produced by OpenType substitution (EB Garamond's f_i, f_l, f_f…)
   have no entry and extract as U+FFFD: copy/paste and search break on "firm".
   The layout does know the characters behind each glyph (one item with the
   glyph, followed by gid == -1 items for the extra characters), so a
   text-only device records those one-glyph-to-many-chars clusters and they
   are appended to the ToUnicode CMaps before saving. */

using GlyphTextMap = std::map<fz_font *, std::map<int, QString>>;

struct ClusterCollector {
  fz_device super;
  GlyphTextMap *map;
};

// No MuPDF call in here (nothing can longjmp across the C++ temporaries).
static void collectClusters(fz_context *, fz_device *dev, const fz_text *text,
                            fz_matrix, fz_colorspace *, const float *, float,
                            fz_color_params) {
  GlyphTextMap &map = *reinterpret_cast<ClusterCollector *>(dev)->map;
  for (const fz_text_span *span = text->head; span; span = span->next) {
    int i = 0;
    while (i < span->len) {
      const fz_text_item &item = span->items[i++];
      if (item.gid < 0 || item.ucs < 0)
        continue;
      char32_t cps[8];
      int n = 0;
      cps[n++] = static_cast<char32_t>(item.ucs);
      while (i < span->len && span->items[i].gid == -1) {
        if (span->items[i].ucs >= 0 && n < 8)
          cps[n++] = static_cast<char32_t>(span->items[i].ucs);
        ++i;
      }
      if (n > 1)
        map[span->font][item.gid] = QString::fromUcs4(cps, n);
    }
  }
}

// "N beginbfchar <gid> <utf16be> … endbfchar" blocks (max 100 entries each).
static QByteArray toBfcharBlocks(const std::map<int, QString> &glyphs) {
  auto hex4 = [](uint v) {
    return QByteArray::number(v, 16).rightJustified(4, '0');
  };
  QByteArray out;
  auto it = glyphs.begin();
  while (it != glyphs.end()) {
    QByteArray entries;
    int count = 0;
    for (; it != glyphs.end() && count < 100; ++it, ++count) {
      // Source: 2-byte Identity-H code (= gid). Destination: UTF-16BE
      // (QString is UTF-16 already, surrogate pairs included).
      QByteArray dst;
      for (const QChar ch : it->second)
        dst += hex4(ch.unicode());
      entries += '<' + hex4(static_cast<uint>(it->first)) + "> <" + dst + ">\n";
    }
    out += '\n' + QByteArray::number(count) + " beginbfchar\n" + entries +
           "endbfchar\n";
  }
  return out;
}

// Insert extra bfchar entries before "endcmap" in the font's ToUnicode CMap.
static void appendToUnicode(fz_context *ctx, pdf_document *pdf, fz_font *font,
                            const QByteArray &entries) {
  pdf_obj *fobj = nullptr;
  fz_buffer *oldMap = nullptr;
  fz_buffer *newMap = nullptr;
  fz_var(fobj);
  fz_var(oldMap);
  fz_var(newMap);
  fz_try(ctx) {
    // Cached per font: returns the object the PDF device already created.
    fobj = pdf_add_cid_font(ctx, pdf, font);
    pdf_obj *toUnicode = pdf_dict_get(ctx, fobj, PDF_NAME(ToUnicode));
    if (pdf_is_stream(ctx, toUnicode)) {
      oldMap = pdf_load_stream(ctx, toUnicode);
      unsigned char *data = nullptr;
      const size_t len = fz_buffer_storage(ctx, oldMap, &data);
      static const char kEnd[] = "endcmap";
      const size_t endLen = sizeof kEnd - 1;
      size_t pos = len;
      for (size_t k = 0; k + endLen <= len; ++k) {
        if (memcmp(data + k, kEnd, endLen) == 0) {
          pos = k;
          break;
        }
      }
      if (pos < len) {
        newMap = fz_new_buffer(ctx, len + static_cast<size_t>(entries.size()));
        fz_append_data(ctx, newMap, data, pos);
        fz_append_data(ctx, newMap, entries.constData(),
                       static_cast<size_t>(entries.size()));
        fz_append_data(ctx, newMap, data + pos, len - pos);
        pdf_update_stream(ctx, pdf, toUnicode, newMap, 0);
      }
    }
  }
  fz_always(ctx) {
    fz_drop_buffer(ctx, newMap);
    fz_drop_buffer(ctx, oldMap);
    pdf_drop_obj(ctx, fobj);
  }
  fz_catch(ctx) { fz_rethrow(ctx); }
}

enum class FooterAlign { Center, Right };

// One line of text on the footer baseline, in the bottom margin.
static void drawFooterText(fz_context *ctx, fz_device *dev, fz_font *font,
                           const char *utf8, float size, float grey,
                           FooterAlign align) {
  // Glyph space is y-up, page space y-down: flip with a negative scale.
  const fz_matrix unit = fz_scale(size, -size);
  const fz_matrix end = fz_measure_string(ctx, font, unit, utf8, 0, 0,
                                          FZ_BIDI_LTR, FZ_LANG_UNSET);
  fz_matrix trm = unit;
  trm.e = align == FooterAlign::Right
              ? kPdfPage.x1 - kPdfMarginX - end.e
              : (kPdfPage.x1 - end.e) / 2.0f;
  trm.f = kPdfPage.y1 - kPdfMarginBottom / 2.0f;
  fz_text *text = fz_new_text(ctx);
  fz_try(ctx) {
    fz_show_string(ctx, text, font, trm, utf8, 0, 0, FZ_BIDI_LTR,
                   FZ_LANG_UNSET);
    const float color[1] = {grey};
    fz_fill_text(ctx, dev, text, fz_identity, fz_device_gray(ctx), color,
                 1.0f, fz_default_color_params);
  }
  fz_always(ctx) { fz_drop_text(ctx, text); }
  fz_catch(ctx) { fz_rethrow(ctx); }
}

// Page footer: optional centered italic note + right-aligned page number.
struct PdfFooter {
  fz_font *numberFont;
  fz_font *noteFont;
  const char *note; // UTF-8, may be empty
};

static void drawFooter(fz_context *ctx, fz_device *dev, const PdfFooter &footer,
                       int pageNo) {
  if (footer.note && *footer.note)
    drawFooterText(ctx, dev, footer.noteFont, footer.note, 8.0f, 0.53f,
                   FooterAlign::Center);
  char label[16];
  snprintf(label, sizeof label, "%d", pageNo);
  drawFooterText(ctx, dev, footer.numberFont, label, 9.0f, 0.25f,
                 FooterAlign::Right);
}

// Draw the currently placed part of the story (plus the footer) on a new
// page appended to the document. The page is recorded in a display list so it
// can be replayed into both the PDF device and the cluster collector.
static void writeStoryPage(fz_context *ctx, pdf_document *pdf, fz_story *story,
                           const PdfFooter &footer, int pageNo,
                           fz_device *collector) {
  fz_display_list *list = nullptr;
  fz_device *listDev = nullptr;
  pdf_obj *resources = nullptr;
  fz_buffer *contents = nullptr;
  fz_device *dev = nullptr;
  pdf_obj *page = nullptr;
  fz_var(list);
  fz_var(listDev);
  fz_var(resources);
  fz_var(contents);
  fz_var(dev);
  fz_var(page);
  fz_try(ctx) {
    list = fz_new_display_list(ctx, kPdfPage);
    listDev = fz_new_list_device(ctx, list);
    fz_draw_story(ctx, story, listDev, fz_identity);
    drawFooter(ctx, listDev, footer, pageNo);
    fz_close_device(ctx, listDev);

    dev = pdf_page_write(ctx, pdf, kPdfPage, &resources, &contents);
    fz_run_display_list(ctx, list, dev, fz_identity, fz_infinite_rect, nullptr);
    fz_close_device(ctx, dev);
    fz_run_display_list(ctx, list, collector, fz_identity, fz_infinite_rect,
                        nullptr);

    page = pdf_add_page(ctx, pdf, kPdfPage, 0, resources, contents);
    pdf_insert_page(ctx, pdf, -1, page);
  }
  fz_always(ctx) {
    pdf_drop_obj(ctx, page);
    fz_drop_device(ctx, dev);
    fz_drop_buffer(ctx, contents);
    pdf_drop_obj(ctx, resources);
    fz_drop_device(ctx, listDev);
    fz_drop_display_list(ctx, list);
  }
  fz_catch(ctx) { fz_rethrow(ctx); }
}

// `footerNote`: if set, printed centered at the bottom of every page and the
// end-of-document attribution (kPdfFooterHtml) is omitted.
static QString writeHtmlToPdf(const QString &html, const QString &destPath,
                              const QString &footerNote = QString()) {
  QString path = destPath;
  if (path.startsWith("file://"))
    path = QUrl(path).toLocalFile();
  if (path.isEmpty())
    return QString();

  const QString fontDir = QCoreApplication::applicationDirPath() +
                          QStringLiteral("/Fonts/EB_Garamond/static");
  const bool haveFontDir = QDir(fontDir).exists();
  if (!haveFontDir)
    qWarning() << "[PDF] Font directory missing, MuPDF will fall back to its "
                  "built-in fonts:"
               << fontDir;
  const QString pageFontPath =
      fontDir + QStringLiteral("/EBGaramond-Regular.ttf");
  const bool havePageFont = QFile::exists(pageFontPath);
  const QString noteFontPath =
      fontDir + QStringLiteral("/EBGaramond-Italic.ttf");
  const bool haveNoteFont = QFile::exists(noteFontPath);

  // Everything owned by Qt is prepared outside fz_try: fz_throw longjmps,
  // which would skip C++ destructors inside the protected block.
  const QByteArray doc =
      (QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
                      "</head><body>") +
       html + (footerNote.isEmpty() ? kPdfFooterHtml : QString()) +
       QStringLiteral("</body></html>"))
          .toUtf8();
  const QByteArray pathUtf8 = path.toUtf8();
  const QByteArray fontDirUtf8 = fontDir.toUtf8();
  const QByteArray pageFontUtf8 = pageFontPath.toUtf8();
  const QByteArray noteFontUtf8 = noteFontPath.toUtf8();
  const QByteArray footerNoteUtf8 = footerNote.toUtf8();
  GlyphTextMap clusters;
  std::vector<std::pair<fz_font *, QByteArray>> toUnicodeFixes;

  fz_context *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
  if (!ctx) {
    qCritical() << "[PDF] Cannot create MuPDF context";
    return QString();
  }

  fz_archive *fonts = nullptr;
  fz_buffer *buf = nullptr;
  fz_story *story = nullptr;
  pdf_document *pdf = nullptr;
  fz_font *pageFont = nullptr;
  fz_font *noteFont = nullptr;
  ClusterCollector *collector = nullptr;
  bool laidOut = false;
  bool ok = false;
  fz_var(fonts);
  fz_var(buf);
  fz_var(story);
  fz_var(pdf);
  fz_var(pageFont);
  fz_var(noteFont);
  fz_var(collector);
  fz_var(laidOut);
  fz_var(ok);

  // 1. Lay out and draw every page.
  fz_try(ctx) {
    if (haveFontDir)
      fonts = fz_open_directory(ctx, fontDirUtf8.constData());
    buf = fz_new_buffer_from_copied_data(
        ctx, reinterpret_cast<const unsigned char *>(doc.constData()),
        static_cast<size_t>(doc.size()));
    story = fz_new_story(ctx, buf, kPdfCss, 11.0f, fonts);
    pageFont = havePageFont
                   ? fz_new_font_from_file(ctx, nullptr,
                                           pageFontUtf8.constData(), 0, 0)
                   : fz_new_base14_font(ctx, "Times-Roman");
    noteFont = haveNoteFont
                   ? fz_new_font_from_file(ctx, nullptr,
                                           noteFontUtf8.constData(), 0, 0)
                   : fz_new_base14_font(ctx, "Times-Italic");
    const PdfFooter footer = {pageFont, noteFont, footerNoteUtf8.constData()};

    collector = fz_new_derived_device(ctx, ClusterCollector);
    collector->super.fill_text = collectClusters;
    collector->map = &clusters;

    // Build the pdf_document ourselves (instead of fz_new_pdf_writer) so the
    // ToUnicode maps can be fixed and the fonts subset before saving: whole
    // EB Garamond faces weigh ~700 KB per PDF, subsets a few dozen.
    pdf = pdf_create_document(ctx);
    fz_set_metadata(ctx, &pdf->super, FZ_META_INFO_CREATOR, "Hodeia");
    fz_set_metadata(ctx, &pdf->super, FZ_META_INFO_PRODUCER, "Hodeia (MuPDF)");

    const fz_rect area = {kPdfMarginX, kPdfMarginTop,
                          kPdfPage.x1 - kPdfMarginX,
                          kPdfPage.y1 - kPdfMarginBottom};
    int more = 1;
    int pageNo = 0;
    while (more) {
      fz_rect filled;
      more = fz_place_story(ctx, story, area, &filled);
      writeStoryPage(ctx, pdf, story, footer, ++pageNo, &collector->super);
    }
    fz_close_device(ctx, &collector->super);
    laidOut = true;
  }
  fz_catch(ctx) {
    qCritical() << "[PDF] MuPDF layout failed:" << fz_caught_message(ctx);
  }

  // 2. Outside fz_try: turn the recorded clusters into CMap entries.
  if (laidOut)
    for (const auto &[font, glyphs] : clusters)
      toUnicodeFixes.emplace_back(font, toBfcharBlocks(glyphs));

  // 3. Patch ToUnicode, subset the fonts, save.
  if (laidOut) {
    fz_try(ctx) {
      for (size_t i = 0; i < toUnicodeFixes.size(); ++i)
        appendToUnicode(ctx, pdf, toUnicodeFixes[i].first,
                        toUnicodeFixes[i].second);
      pdf_subset_fonts(ctx, pdf, 0, nullptr);
      pdf_write_options opts;
      pdf_init_write_options(ctx, &opts);
      opts.do_compress = 1;
      opts.do_compress_fonts = 1;
      opts.do_compress_images = 1;
      opts.do_garbage = 3; // drop the unsubset font streams, de-duplicate
      opts.do_use_objstms = 1;
      pdf_save_document(ctx, pdf, pathUtf8.constData(), &opts);
      ok = true;
    }
    fz_catch(ctx) {
      qCritical() << "[PDF] MuPDF save failed:" << fz_caught_message(ctx);
    }
  }

  fz_drop_device(ctx, collector ? &collector->super : nullptr);
  pdf_drop_document(ctx, pdf);
  fz_drop_font(ctx, noteFont);
  fz_drop_font(ctx, pageFont);
  fz_drop_story(ctx, story);
  fz_drop_buffer(ctx, buf);
  fz_drop_archive(ctx, fonts);
  fz_drop_context(ctx);

  if (!ok) {
    QFile::remove(path); // don't leave a truncated file behind
    return QString();
  }
  return QFile::exists(path) ? path : QString();
}

QString Helpers::exportSummaryToPdf(const QString &json,
                                    const QString &destPath) const {
  if (json.isEmpty())
    return QString();
  return writeHtmlToPdf(toHtml(parseSummaryJson(json)), destPath);
}

QString Helpers::generateReport(const QStringList &jsonList,
                                const QString &destPath) const {
  if (jsonList.isEmpty())
    return QString();

  QStringList sections;
  for (const QString &json : jsonList) {
    if (json.isEmpty())
      continue;
    sections.append(toHtml(parseSummaryJson(json)));
  }
  if (sections.isEmpty())
    return QString();

  // One paper per page: MuPDF ignores "page-break-after: avoid", so a summary
  // flowing on from the previous one could leave its title block stranded at
  // the bottom of a page.
  QString html =
      "<h1 class=\"center\">Research Summary Report</h1>"
      "<hr><div>" +
      sections.join("</div><div class=\"new-page\">") + "</div>";
  return writeHtmlToPdf(html, destPath, QStringLiteral("Made with Hodeia"));
}

/* ── Local model listing ─────────────────────────────────────── */

void Helpers::refreshLocalModelList() {
  if (!m_config)
    return;
  QDir d(m_config->summarizersPath());
  if (!d.exists()) {
    m_localModelList.clear();
    emit localModelListChanged();
    return;
  }
  QStringList list = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  if (list != m_localModelList) {
    m_localModelList = list;
    emit localModelListChanged();
  }
}

QStringList Helpers::localModelList() { return m_localModelList; }

void Helpers::deleteLocalModels(const QStringList &folderNames) {
  if (!m_config)
    return;
  for (const QString &name : folderNames) {
    if (name.trimmed().isEmpty())
      continue;
    QString folder = m_config->summarizersPath() + "/" + name.trimmed();
    QDir dir(folder);
    if (dir.exists()) {
      dir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
      for (const QFileInfo &entry : dir.entryInfoList()) {
        if (entry.isDir())
          QDir(entry.absoluteFilePath()).removeRecursively();
        else
          QFile::remove(entry.absoluteFilePath());
      }
      dir.rmdir(folder);
      qDebug() << "[Helpers] Local model removed:" << folder;
    }
  }
  refreshLocalModelList();
  loadActiveBackendModels();
}

QStringList Helpers::listLocalModels() {
  if (!m_config)
    return {};
  QString dir = m_config->summarizersPath();
  QDir d(dir);
  if (!d.exists())
    return {};
  QStringList models;
  for (const QString &subdir : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    QStringList ggufs =
        QDir(dir + "/" + subdir)
            .entryList(QStringList{"*.gguf"}, QDir::Files | QDir::NoSymLinks);
    for (const QString &f : ggufs) {
      if (!f.contains("mmproj", Qt::CaseInsensitive)) {
        models.append(subdir);
        break;
      }
    }
  }
  return models;
}

QStringList Helpers::getImagesForHash(const QString &paperHash,
                                      const QString &imgPath) const {
  QDir imgDir(imgPath);
  if (!imgDir.exists())
    return {};
  imgDir.setNameFilters({paperHash + "_page_*.png"});
  QStringList paths;
  for (const QFileInfo &fi : imgDir.entryInfoList())
    paths.append(fi.absoluteFilePath());
  paths.sort();
  return paths;
}

QString Helpers::localFileUrl(const QString &path) const {
  return QUrl::fromLocalFile(path).toString();
}

/* ── Remote backend ──────────────────────────────────────────── */

void Helpers::checkRemoteModels(const QString &name, const QString &apiUrl,
                                const QString &apiKey) {
  if (apiUrl.isEmpty()) {
    emit modelsChecked(false, {}, tr("Invalid parameters"));
    return;
  }
  QObject::connect(
      this, &Helpers::modelsChecked, this,
      [this, name, apiUrl, apiKey](bool ok, const QStringList &,
                                   const QString &) {
        if (ok && m_config) {
          m_config->setBackend(name, apiUrl, apiKey);
          if (m_config->activeBackend() != name)
            m_config->setActiveBackend(name);
        }
      },
      Qt::SingleShotConnection);
  fetchRemoteModels(apiUrl, apiKey);
}

void Helpers::fetchRemoteModels(const QString &apiUrl, const QString &apiKey) {
  if (apiUrl.isEmpty()) {
    emit modelsChecked(false, {}, tr("URL is empty"));
    return;
  }
  if (m_currentReply) {
    m_currentReply->abort();
    m_currentReply = nullptr;
  }
  QUrl url(apiUrl);
  url.setPath(url.path() + "/models");
  QNetworkRequest req(url);
  if (!apiKey.isEmpty())
    req.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());

  m_currentReply = m_networkManager->get(req);
  QObject::connect(m_currentReply, &QNetworkReply::finished, this,
                   [this, reply = m_currentReply]() {
                     if (m_currentReply == reply)
                       m_currentReply = nullptr;
                     handleModelsReply(reply);
                   });
}

void Helpers::handleModelsReply(QNetworkReply *reply) {
  if (!reply)
    return;
  if (reply->error() != QNetworkReply::NoError) {
    emit modelsChecked(false, {}, reply->errorString());
    return;
  }
  auto doc = QJsonDocument::fromJson(reply->readAll());
  if (!doc.isObject()) {
    emit modelsChecked(false, {}, tr("Invalid JSON"));
    return;
  }
  QStringList models;
  for (const auto &e : doc.object().value("data").toArray()) {
    QString id = e.toObject().value("id").toString();
    if (!id.isEmpty())
      models.append(id);
  }
  qDebug() << "[Helpers] Received" << models.size() << "model(s) from server";
  emit modelsChecked(true, models, QString());
}

void Helpers::loadActiveBackendModels() {
  if (!m_config)
    return;
  QVariantMap cfg = m_config->backendConfig(m_config->activeBackend());
  QString url = cfg.value("url").toString();
  if (url.isEmpty()) {
    QStringList m = listLocalModels();
    emit modelsChecked(!m.isEmpty(), m, QString());
  } else {
    checkRemoteModels(cfg.value("name").toString(), url,
                      cfg.value("api_key").toString());
  }
}

/* ── Model copy (parallel, non-blocking) ─────────────────────── */

void Helpers::addLocalSummarizer(const QString &sourcePath) {
  addLocalSummarizerWithMmproj(sourcePath, QString());
}

// Normalize a path that may arrive as a file: URL (from QFileDialog's
// selectedFile) or as a raw native path (after a QML "file://" strip).
// QUrl::fromUserInput round-trips both forms to the correct local path on
// every platform, so the downstream copy logic always gets a clean path.
static QString normalizeLocalPath(const QString &in) {
  if (in.isEmpty())
    return {};
  if (in.startsWith(QLatin1String("file://"), Qt::CaseInsensitive))
    return QUrl(in).toLocalFile();
  return QUrl::fromUserInput(in).toLocalFile();
}

void Helpers::addLocalSummarizerWithMmproj(const QString &modelPath,
                                           const QString &mmprojPath) {
  if (!m_config)
    return;

  const QString modelSrc = normalizeLocalPath(modelPath);
  const QString mmprojSrc = normalizeLocalPath(mmprojPath);

  if (!QFile::exists(modelSrc)) {
    emit modelCopyFinished(false, QString(),
                           tr("Model file does not exist: %1").arg(modelSrc));
    return;
  }
  if (!mmprojSrc.isEmpty() && !QFile::exists(mmprojSrc)) {
    emit modelCopyFinished(false, QString(),
                           tr("MMProj file does not exist: %1").arg(mmprojSrc));
    return;
  }

  QString modelName = QFileInfo(modelSrc).completeBaseName();
  QString modelFolder = m_config->summarizersPath() + "/" + modelName;
  QDir().mkpath(modelFolder);

  auto *done = new QAtomicInt(!mmprojSrc.isEmpty() ? 2 : 1);
  QAtomicInt *modelOk = new QAtomicInt{0};
  QAtomicInt *mmprojOk = new QAtomicInt{1};

  Helpers *self = this;

  QThreadPool::globalInstance()->start(
      [self, src = modelSrc, folder = modelFolder, done, modelOk]() {
        *modelOk = copyFileToDir(src, folder, self) ? 1 : 0;
        done->fetchAndAddRelaxed(-1);
      });

  if (!mmprojSrc.isEmpty()) {
    QThreadPool::globalInstance()->start(
        [self, src = mmprojSrc, folder = modelFolder, done, mmprojOk]() {
          *mmprojOk = copyFileToDir(src, folder, self) ? 1 : 0;
          done->fetchAndAddRelaxed(-1);
        });
  }

  auto *timer = new QTimer(this);
  timer->setInterval(50);
  QObject::connect(
      timer, &QTimer::timeout, timer, [timer, done, modelOk, mmprojOk, self]() {
        if (done->loadRelaxed() == 0) {
          bool m = modelOk->loadRelaxed();
          bool mp = mmprojOk->loadRelaxed();
          delete done;
          delete modelOk;
          delete mmprojOk;
          Q_EMIT self->modelCopyFinished(m && mp, QString(), QString());
          if (m && mp)
            Q_EMIT self->modelCopied();
          timer->stop();
          timer->deleteLater();
        }
      });
  timer->start();
}
