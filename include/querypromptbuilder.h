#pragma once

#include "localprompt.h"
#include "pagecontext.h"

#include <QList>
#include <QString>

/// The prompt of a query over the library: instructions, the retrieved page
/// contexts (numbered for citation, see CitationFormat) and the question.
namespace QueryPrompt {

/// System prompt: role, citation, mathematics and layout rules.
const QString &systemPrompt();

/// Formatting reminder repeated at the end of the user message.
const QString &formatReminder();

/// Text every query prompt carries whatever the pages (system prompt,
/// reminder, question): what the page budget must leave room for.
QString fixedText(const QString &question);

/// The prompt for `question` over `pages` (in citation order: page context
/// [n] is pages[n-1]).  `useVisual`: attach the images of the pages marked
/// withImage, loaded from their imageFile.
PromptPayload build(const QList<PageContext> &pages, const QString &question,
                    bool useVisual);

} // namespace QueryPrompt
