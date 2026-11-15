#pragma once

#include <QList>
#include <QString>
#include <QStringList>

/// Paper-level summary produced by the LLM.
struct PaperSummary {
  QString title;
  QStringList authors;
  QString journal;
  int year = 0;
  QStringList topics;
  QString researchObjective;
  QString methodology;
  QStringList coreFindingsAndSignificance;
  QStringList criticalLimitations;
  QString citableSummary;
};

/// Parsing of the LLM's JSON summaries and their HTML rendering (shared by
/// the QML display and the PDF export).
namespace SummaryFormat {

/// Parse a raw LLM JSON response into a PaperSummary. Code fences, reasoning
/// text and common malformed-JSON patterns are tolerated; an unrecoverable
/// response yields an empty summary.
PaperSummary parseJson(const QString &raw);

/// HTML body fragment for one summary. No font family is set: each renderer
/// (QML, PDF) supplies its own.
QString toHtml(const PaperSummary &summary);

/// HTML body fragment for a report: the summaries one after another,
/// separated by a rule, under a report title when there is more than one.
QString reportToHtml(const QList<PaperSummary> &papers);

} // namespace SummaryFormat
