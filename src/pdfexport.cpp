#include "pdfexport.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QUrl>

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

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
    ".center { text-align: center; }";

// Attribution printed centered at the bottom of every page.
static const char kPdfFooterNote[] = "Made with HodeIA";

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

// Page footer: centered italic note + right-aligned page number.
struct PdfFooter {
  fz_font *numberFont;
  fz_font *noteFont;
};

static void drawFooter(fz_context *ctx, fz_device *dev, const PdfFooter &footer,
                       int pageNo) {
  drawFooterText(ctx, dev, footer.noteFont, kPdfFooterNote, 8.0f, 0.53f,
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

namespace PdfExport {

QString writeHtml(const QString &html, const QString &destPath) {
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
       html + QStringLiteral("</body></html>"))
          .toUtf8();
  const QByteArray pathUtf8 = path.toUtf8();
  const QByteArray fontDirUtf8 = fontDir.toUtf8();
  const QByteArray pageFontUtf8 = pageFontPath.toUtf8();
  const QByteArray noteFontUtf8 = noteFontPath.toUtf8();
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
    const PdfFooter footer = {pageFont, noteFont};

    collector = fz_new_derived_device(ctx, ClusterCollector);
    collector->super.fill_text = collectClusters;
    collector->map = &clusters;

    // Build the pdf_document ourselves (instead of fz_new_pdf_writer) so the
    // ToUnicode maps can be fixed and the fonts subset before saving: whole
    // EB Garamond faces weigh ~700 KB per PDF, subsets a few dozen.
    pdf = pdf_create_document(ctx);
    fz_set_metadata(ctx, &pdf->super, FZ_META_INFO_CREATOR, "HodeIA");
    fz_set_metadata(ctx, &pdf->super, FZ_META_INFO_PRODUCER, "HodeIA (MuPDF)");

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

} // namespace PdfExport
