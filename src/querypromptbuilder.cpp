#include "querypromptbuilder.h"

#include "metadataresolver.h"

#include <QDebug>
#include <QImage>
#include <QStringList>

namespace {

const QString s_systemPrompt = []() {
  return R"(You are an expert academic research assistant.
You are provided with highly relevant pages extracted from multiple research papers.
Each page is presented with its image (when available) followed by its extracted text.
Synthesize a cohesive, detailed, comprehensive response answering the user's query utilizing both
the textual evidence and visual figures/tables/charts provided.

Citations: each page context is labelled with a reference number in square brackets, e.g. [3].
Cite every claim taken from the context inline using ONLY these reference numbers,
e.g. "... latent classes [3]." or "... as shown in [2][5]."
Never write author names, years or page numbers in a citation yourself, and ignore any page
numbers printed inside the page text: the reference numbers are converted into full citations
automatically. Do NOT add a references, bibliography or sources section at the end.

Mathematics: NEVER use LaTeX ($...$, $$...$$, \(...\), \frac, \text, \epsilon, _{...}, ^{...}).
Write formulas as plain Unicode text: Greek letters (α β γ ε θ μ σ π),
superscripts/subscripts (x², xᵢ, Uᵢₙ), and symbols (√ ± × ÷ ≠ ≤ ≥ ≈ ∑ ∫ → ∞ Δ ∈ ∀).
Prefer a flat, readable form (e.g. "Uᵢₙ = V(Xₙ, Xᵢ; b) + εᵢₙ", "μ = 5.2, σ = 1.3");
write fractions as "a/b".

Layout: output the response in Markdown, structured for easy reading.
- Start with a short introduction (2-3 sentences).
- Organize the body into sections with "### " headings.
- Keep paragraphs short: 2-4 sentences each, one idea per paragraph,
  separated by a blank line. Never write a paragraph longer than 5 sentences.
- Use bullet lists ("- **Term:** explanation") for enumerations of factors,
  findings or results, with a blank line before the list.
- End with a short "### Conclusion" section.)";
}();

/// Formatting reminder appended at the very end of the user message: models
/// weigh the end of the prompt most, and the system prompt alone is often
/// ignored after a long context.
const QString s_formatReminder = QStringLiteral(
    "Formatting rules (mandatory): cite only with the bracketed reference "
    "numbers of the page contexts, e.g. [3] or [2][5] — no author names or "
    "page numbers; no final references section; no LaTeX at all — write math "
    "in plain Unicode (εᵢₙ, x², ≠, ≤); \"### \" section headings; short "
    "paragraphs of 2-4 sentences separated by blank lines; bullet lists for "
    "enumerations.");

} // namespace

namespace QueryPrompt {

const QString &systemPrompt() { return s_systemPrompt; }

const QString &formatReminder() { return s_formatReminder; }

QString fixedText(const QString &question) {
  return s_systemPrompt + s_formatReminder + question;
}

PromptPayload build(const QList<PageContext> &pages, const QString &question,
                    bool useVisual) {
  PromptPayload payload;
  payload.systemPrompt = s_systemPrompt;

  // Build structured text context: one block per page.
  QStringList blocks;
  for (int i = 0; i < pages.size(); ++i) {
    const PageContext &pc = pages[i];
    const QString src = MetadataResolver::paperLabel(pc).sourceForm();

    // The bracketed number is the reference tag the model cites with; it is
    // resolved back to this page by CitationFormat::resolveReferenceTags().
    QString block = QString("=== PAGE CONTEXT [%1] — %2, page %3 ===\n")
                        .arg(i + 1)
                        .arg(src)
                        .arg(pc.displayPage());

    for (const QString &chunk : pc.textChunks)
      block += chunk + "\n[---]\n";

    block += "\n";
    blocks.append(block);

    // Build PageBlock with image
    PageBlock pb;
    pb.pageNumber = i + 1;
    pb.text = block;

    if (useVisual && pc.withImage) {
      QImage img(pc.imageFile);
      if (!img.isNull()) {
        pb.visualContext = PageImage::forModel(img);
        // Legacy (non-interleaved) multimodal path: reuse the decoded pixels
        // instead of decoding the PNG a second time.
        payload.visualContext.push_back(pb.visualContext);
      }
    }
    payload.pageBlocks.push_back(std::move(pb));
  }

  payload.textContext = blocks.join("\n");
  payload.userMessage = "Core Task Query: " + question + "\n\n" +
                        s_formatReminder +
                        "\n\nSynthesize your final cross-paper response now:";

  if (useVisual)
    qDebug() << "[QueryPrompt] Visual context —" << payload.visualContext.size()
             << "image(s) loaded";

  return payload;
}

} // namespace QueryPrompt
