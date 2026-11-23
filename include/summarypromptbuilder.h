#pragma once

#include "configmanager.h" // VisualMode
#include "localprompt.h"
#include "sqlitevec.h"     // ChunkSearchResult

#include <QList>
#include <QMap>
#include <QString>

/// The prompt asking for the structured JSON summary of one paper: its text
/// page by page, with page images depending on the visual mode.
namespace SummaryPrompt {

/// System prompt when page images are sent.
const QString &visualSystemPrompt();
/// System prompt for text only (also the fallback of a backend that cannot
/// pass the images).
const QString &textSystemPrompt();
/// The user message.
const QString &userPrompt();

/// Length of the text `next` repeats from the end of `previous`: the
/// overlap the ingestion carries from one chunk to the next (whole
/// sentences joined by spaces, so it starts and ends at a space or at the
/// ends of the chunks).  0 if none.
qsizetype chunkOverlap(QStringView previous, QStringView next);

/// The text of each page (0-based page → text) rebuilt from a paper's
/// `chunks` (in chunk order), each chunk without the overlap it repeats
/// from the previous one.
QMap<int, QString> pageTexts(const QList<ChunkSearchResult> &chunks);

/// The prompt for the paper `paperHash` from its `chunks` (in chunk order).
/// `mode` decides which pages get their image: all (Full), those whose
/// metadata reports a figure, vector graphics or a table (Reduced), none
/// (TextOnly).  Images are read from `imageDir` (hash_page_NNN.png); a
/// missing one leaves its page text-only.
PromptPayload build(const QString &paperHash,
                    const QList<ChunkSearchResult> &chunks, VisualMode mode,
                    const QString &imageDir);

} // namespace SummaryPrompt
