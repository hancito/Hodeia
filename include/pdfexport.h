#pragma once

#include <QString>

/// HTML → PDF rendering with MuPDF's HTML/CSS engine (fz_story): A4 pages,
/// EB Garamond, page numbers and a "Made with HodeIA" note on every page.
namespace PdfExport {

/// Lay out the HTML body fragment @p html and save it to @p destPath (a local
/// path or a file: URL). Returns the saved path, or an empty string on failure
/// (no partial file is left behind).
QString writeHtml(const QString &html, const QString &destPath);

} // namespace PdfExport
