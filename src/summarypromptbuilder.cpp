#include "summarypromptbuilder.h"

#include <QDebug>
#include <QDir>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <algorithm>

namespace {

const QString s_systemPromptVisual = []() {
  return R"(You are Senior Research Analyst. Your task is to analyze academic papers with extreme precision. When asked to summarize a paper you will:
  1. The document is provided page by page. Each page's image (when available) precedes its extracted text.
  2. Use the images to gather broader context and verify data, tables, and figures alongside the OCR text.
  3. And respond with high details following this structured schema in english and don't put icons:
  4. Use mathematical description when pertinent
{
  "title": "The full title of the paper",
  "authors": [A list of author names],
  "journal": "journal or conference name",
  "year": "The publication year as an integer",
  "topics": [A list the 5 most relevant topics or keywords from the paper.],
  "researchObjective": "Identify the primary question or hypothesis in 1 sentence",
  "methodology": "Describe the study design, sample size, and key variables. Use a paragraph",
  "coreFindingsAndSignificance": ["Highlight the most statistically or theoretically significant results. Use a bulleted list"],
  "criticalLimitations": ["Identify a list of internal or external validity threats mentioned or observed.",]
  "citableSummary": "A two-sentence summary suitable for a literature review"
}
For mathematics, do NOT use LaTeX or markdown. Write formulas as plain
Unicode text so they render directly: use Greek letters (α β γ θ μ σ π),
superscripts/subscripts (x², xᵢ, aⱼ), and symbols (√, ², ³, ±, ×, ÷, ≤, ≥,
≈, ∑, ∫, →, ∞, Δ). Prefer a flat, readable form (e.g. "x² + y² = r²",
"μ = 5.2, σ = 1.3") over stacked fractions — write fractions as "a/b".
Don't use markdown formatting. Return only valid JSON.)";
}();

const QString s_systemPromptText = []() {
  return R"(You are Senior Research Analyst. Your task is to analyze academic papers with extreme precision. When asked to summarize a paper you will:
  1. Read the provided text context extracted from the paper, organized page by page.
  2. And respond with high details following this structured schema in english and don't put icons:
  3. Use mathematical description when pertinent
{
  "title": "The full title of the paper",
  "authors": [A list of author names],
  "journal": "journal or conference name",
  "year": "The publication year as an integer",
  "topics": [A list the 5 most relevant topics or keywords from the paper.],
  "researchObjective": "Identify the primary question or hypothesis in 1 sentence",
  "methodology": "Describe the study design, sample size, and key variables. Use a paragraph",
  "coreFindingsAndSignificance": ["Highlight the most statistically or theoretically significant results. Use a bulleted list"],
  "criticalLimitations": ["Identify a list of internal or external validity threats mentioned or observed.",]
  "citableSummary": "A two-sentence summary suitable for a literature review"
}
For mathematics, do NOT use LaTeX or markdown. Write formulas as plain
Unicode text so they render directly: use Greek letters (α β γ θ μ σ π),
superscripts/subscripts (x², xᵢ, aⱼ), and symbols (√, ², ³, ±, ×, ÷, ≤, ≥,
≈, ∑, ∫, →, ∞, Δ). Prefer a flat, readable form (e.g. "x² + y² = r²",
"μ = 5.2, σ = 1.3") over stacked fractions — write fractions as "a/b".
Don't use markdown formatting. Return only valid JSON.)";
}();

const QString s_userPrompt = []() {
  return R"(Summarize this paper:)";
}();

} // namespace

namespace SummaryPrompt {

const QString &visualSystemPrompt() { return s_systemPromptVisual; }
const QString &textSystemPrompt() { return s_systemPromptText; }
const QString &userPrompt() { return s_userPrompt; }

PromptPayload build(const QString &paperHash,
                    const QList<ChunkSearchResult> &chunks, VisualMode mode,
                    const QString &imageDir) {
  PromptPayload payload;
  payload.userMessage = s_userPrompt;
  if (chunks.isEmpty())
    return payload;

  // ── Merge chunks per page (0-based page → text) ──
  QMap<int, QString> pageTextMap;
  for (int i = 0; i < chunks.size(); ++i) {
    const auto &c = chunks[i];
    QString text = c.text;
    if (i < chunks.size() - 1 && text.length() > ChunkOverlapChars)
      text = text.left(text.length() - ChunkOverlapChars);
    pageTextMap[c.page] += text;
  }

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
  payload.systemPrompt = visual ? s_systemPromptVisual : s_systemPromptText;
  if (visual)
    payload.textOnlySystemPrompt = s_systemPromptText;

  const char *modeName = mode == VisualMode::Full      ? "FULL"
                         : mode == VisualMode::Reduced ? "REDUCED"
                                                       : "TEXT-ONLY";
  qDebug() << "[SummaryPrompt] Visual:" << modeName << "mode —"
           << payload.pageBlocks.size() << "page(s)," << imgCount
           << "image(s) for" << paperHash;

  return payload;
}

} // namespace SummaryPrompt
