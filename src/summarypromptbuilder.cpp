#include "summarypromptbuilder.h"

#include "prompts.h"

#include <QDebug>
#include <QDir>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <algorithm>


namespace SummaryPrompt {


qsizetype chunkOverlap(QStringView previous, QStringView next) {
  // The longest common end/start, at a sentence boundary on both sides
  // (the overlap is the previous chunk's last pieces, joined by spaces).
  // Never the whole next chunk: it always adds something.
  for (qsizetype n = qMin(previous.size(), next.size() - 1); n > 0; --n) {
    if ((n < next.size() && next[n] != u' ') ||
        (n < previous.size() && previous[previous.size() - n - 1] != u' '))
      continue;
    if (previous.endsWith(next.first(n)))
      return n;
  }
  return 0;
}

QMap<int, QString> pageTexts(const QList<ChunkSearchResult> &chunks) {
  QMap<int, QString> pages;
  for (int i = 0; i < chunks.size(); ++i) {
    const auto &c = chunks[i];
    // The overlap stays with the previous chunk (where it comes from).
    QStringView text(c.text);
    if (i > 0)
      text = text.sliced(chunkOverlap(chunks[i - 1].text, text));
    QString &page = pages[c.page];
    if (page.isEmpty())
      text = text.trimmed();
    // Chunks were cut between sentences: keep them apart.
    if (!page.isEmpty() && !page.back().isSpace() && !text.isEmpty() &&
        !text.front().isSpace())
      page += u' ';
    page += text;
  }
  return pages;
}

PromptPayload build(const QString &paperHash,
                    const QList<ChunkSearchResult> &chunks, VisualMode mode,
                    const QString &imageDir, const QString &language) {
  PromptPayload payload;
  payload.userMessage = Prompts::summaryUser();
  if (chunks.isEmpty())
    return payload;

  // ── Merge chunks per page (0-based page → text) ──
  const QMap<int, QString> pageTextMap = pageTexts(chunks);

  // ── Which pages get an image? ──
  QSet<int> pagesWithImage;
  if (mode == VisualMode::Full) {
    for (int p : pageTextMap.keys())
      pagesWithImage.insert(p);
  } else if (mode == VisualMode::Reduced) {
    for (const auto &c : chunks) {
      if (!c.pageMetadata.isEmpty()) {
        QJsonObject meta =
            QJsonDocument::fromJson(c.pageMetadata.toUtf8()).object();
        if (meta.value("has_figure").toBool(false) ||
            meta.value("has_vector_graphics").toBool(false) ||
            meta.value("has_table").toBool(false))
          pagesWithImage.insert(c.page);
      }
    }
  }

  // ── Load page images from disk ──
  QMap<int, QImage> pageImages;
  if (!pagesWithImage.isEmpty()) {
    QDir imgDir(imageDir);
    if (!imageDir.isEmpty() && imgDir.exists()) {
      for (int page0 : pagesWithImage) {
        QString fileName = QString("%1_page_%2.png")
                               .arg(paperHash)
                               .arg(page0 + 1, 3, 10, QChar('0'));
        QImage img(imgDir.absoluteFilePath(fileName));
        if (!img.isNull())
          pageImages[page0] = img;
      }
    }
  }

  // ── Build ordered PageBlocks (1-based) ──
  auto sortedPages = pageTextMap.keys();
  std::sort(sortedPages.begin(), sortedPages.end());

  for (int page0 : sortedPages) {
    PageBlock pb;
    pb.pageNumber = page0 + 1;
    pb.text = pageTextMap[page0];
    auto it = pageImages.find(page0);
    if (it != pageImages.end())
      pb.visualContext = PageImage::forModel(it.value());
    payload.pageBlocks.push_back(std::move(pb));
  }

  // ── System prompt selection ──
  // The visual prompt only when images actually made it into the payload
  // (Reduced mode on a paper without figures has none).  Backends that
  // cannot pass the images fall back to textOnlySystemPrompt.
  int imgCount = 0;
  for (const auto &pb : payload.pageBlocks)
    if (!pb.visualContext.data.empty())
      ++imgCount;
  const bool visual = mode != VisualMode::TextOnly && imgCount > 0;
  payload.systemPrompt = Prompts::summarySystem(visual, language);
  if (visual)
    payload.textOnlySystemPrompt = Prompts::summarySystem(false, language);

  const char *modeName = mode == VisualMode::Full      ? "FULL"
                         : mode == VisualMode::Reduced ? "REDUCED"
                                                       : "TEXT-ONLY";
  qDebug() << "[SummaryPrompt] Visual:" << modeName << "mode —"
           << payload.pageBlocks.size() << "page(s)," << imgCount
           << "image(s) for" << paperHash;

  return payload;
}

} // namespace SummaryPrompt
