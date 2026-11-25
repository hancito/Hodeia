#pragma once

#include <QString>
#include <QStringList>

/// Every text HodeIA sends to a model, in one place: the summary, query and
/// agent prompts and the labels that frame a paper's pages.  The prompts are
/// written in English (models follow English instructions best); the
/// language of the answer is a parameter.
///
/// `language` is an output-language code (ConfigManager::outputLanguage):
///  - answers to a user's question go in the language the user asks for,
///    else in the question's language, else in this language;
///  - summaries (no question) go in the language the user asks for (agent
///    chat), else in this language.
/// Titles, authors and journals of a summary always stay as printed in the
/// paper, and the JSON keys and citation marks never change.
namespace Prompts {

/// Output-language codes offered in the settings, the default first.
const QStringList &outputLanguages();
/// Default output language: English, as HodeIA's summaries always were.
inline const QString kDefaultLanguage = QStringLiteral("en");
/// The language's English name for the model ("fr" → "French"); English
/// for an unknown code.
QString languageName(const QString &language);
/// The language's own name for the settings menu ("fr" → "Français").
QString nativeName(const QString &language);

/* ── Summaries (Summarizer, save_summary) ── */

/// System prompt of a summary; `withImages` when page images precede the
/// page texts.
QString summarySystem(bool withImages, const QString &language);
/// The user message of a summary.
QString summaryUser();
/// What save_summary asks of the agent's text (MCP tool description).
QString summaryLanguageRule(const QString &language);

/* ── Library queries (Query window, classic mode) ── */

QString querySystem(const QString &language);
/// Formatting and language rules repeated at the end of the user message:
/// models weigh the end of a long prompt most.
QString queryReminder(const QString &language);
/// Header of one page given to the model; `reference` is the number it cites
/// with ([n]).
QString queryPageHeader(int reference, const QString &source, int page);
/// The user message around the question (the page texts come apart).
QString queryUserMessage(const QString &question, const QString &language);

/* ── Agent chat (Query window, agent mode) ── */

QString agentSystem(const QString &language);
/// The conversation so far, in front of a new message when the agent could
/// not resume its session.  `turns` are "User: …" / "Assistant: …" lines.
QString agentTranscript(const QStringList &turns);
QString agentUserTurn(const QString &content);
QString agentAssistantTurn(const QString &content);

/* ── A paper's pages, for every backend ── */

/// Heading before the pages of a paper.
QString documentHeading();
/// Heading of page `page` (1-based).
QString pageHeading(int page);
/// Appended where a prompt was cut to fit.
QString truncatedMark();

} // namespace Prompts
