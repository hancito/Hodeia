#pragma once

#include "localprompt.h"
#include "pagecontext.h"
#include "prompts.h"

#include <QList>
#include <QString>

/// The prompt of a query over the library: instructions, the retrieved page
/// contexts (numbered for citation, see CitationFormat) and the question.
namespace QueryPrompt {

/// Text every query prompt carries whatever the pages (system prompt,
/// reminder, question): what the page budget must leave room for.
QString fixedText(const QString &question,
                  const QString &language = Prompts::kDefaultLanguage);

/// The prompt for `question` over `pages` (in citation order: page context
/// [n] is pages[n-1]).  `useVisual`: attach the images of the pages marked
/// withImage, loaded from their imageFile.  The texts are Prompts'; the
/// answer goes in the question's language, `language` when it cannot be told.
PromptPayload build(const QList<PageContext> &pages, const QString &question,
                    bool useVisual,
                    const QString &language = Prompts::kDefaultLanguage);

} // namespace QueryPrompt
