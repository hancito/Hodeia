#pragma once

#include <QString>

/// Post-processing helpers for LLM output, shared by QueryWorker (RAG
/// answers) and Helpers::parseSummaryJson (paper summaries).
namespace TextFormat {

/// Target of the converted text: Markdown output also escapes the characters
/// ('*', '_') that would otherwise be read as emphasis inside formulas.
enum class Target { PlainText, Markdown };

/// Replace LaTeX math spans ($$…$$, \[…\], \(…\), $…$) with readable Unicode
/// (εᵢₙ, x², ≠, a/b…). Models often emit LaTeX despite being told not to,
/// and neither Qt's MarkdownText nor the HTML views can render it.
/// Inline "$…$" must hug its content so currency ("$5 and $10") is kept.
/// For Target::Markdown, fenced and inline code spans are left untouched.
QString latexToUnicode(const QString &text, Target target);

/// Escape the characters Markdown would interpret so @p text renders verbatim.
QString escapeMarkdown(const QString &text);

/// Make an LLM's Markdown render as intended in Qt's MarkdownText.  Models
/// often write a section title as a bold-only line followed by a single
/// newline ("**Title**\nText…"); in Markdown that soft break joins the title
/// to the paragraph.  Such lines become "### Title" headings; headings and
/// lists that follow a paragraph get blank lines around them.  Fenced code
/// blocks are left untouched.
QString normalizeMarkdown(const QString &text);

} // namespace TextFormat
