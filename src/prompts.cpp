#include "prompts.h"


namespace {

struct Language {
  QString code;
  QString english; ///< for the model
  QString native;  ///< for the menu
};

const QList<Language> &languages() {
  static const QList<Language> kLanguages = {
      {"en", "English", "English"},       {"fr", "French", "Français"},
      {"es", "Spanish", "Español"},       {"de", "German", "Deutsch"},
      {"it", "Italian", "Italiano"},      {"pt", "Portuguese", "Português"},
  };
  return kLanguages;
}

const Language &find(const QString &code) {
  for (const Language &l : languages())
    if (l.code == code)
      return l;
  return languages().first();
}

/// Plain-Unicode mathematics, shared by the summary prompts.
const QString kSummaryMath = QStringLiteral(
    "For mathematics, do NOT use LaTeX or markdown. Write formulas as plain\n"
    "Unicode text so they render directly: use Greek letters (α β γ θ μ σ π),\n"
    "superscripts/subscripts (x², xᵢ, aⱼ), and symbols (√, ², ³, ±, ×, ÷, ≤, ≥,\n"
    "≈, ∑, ∫, →, ∞, Δ). Prefer a flat, readable form (e.g. \"x² + y² = r²\",\n"
    "\"μ = 5.2, σ = 1.3\") over stacked fractions — write fractions as \"a/b\".\n"
    "Don't use markdown formatting. Return only valid JSON.");

const QString kSummarySchema = QStringLiteral(
    "{\n"
    "  \"title\": \"The full title of the paper\",\n"
    "  \"authors\": [A list of author names],\n"
    "  \"journal\": \"journal or conference name\",\n"
    "  \"year\": \"The publication year as an integer\",\n"
    "  \"topics\": [A list the 5 most relevant topics or keywords from the paper.],\n"
    "  \"researchObjective\": \"Identify the primary question or hypothesis in 1 sentence\",\n"
    "  \"methodology\": \"Describe the study design, sample size, and key variables. Use a paragraph\",\n"
    "  \"coreFindingsAndSignificance\": [\"Highlight the most statistically or theoretically significant results. Use a bulleted list\"],\n"
    "  \"criticalLimitations\": [\"Identify a list of internal or external validity threats mentioned or observed.\",]\n"
    "  \"citableSummary\": \"A two-sentence summary suitable for a literature review\"\n"
    "}");

/// How answers to a user's message choose their language.
QString answerLanguageRule(const QString &language) {
  return QString("Language: if the user says which language to answer in, use "
                 "it. Otherwise answer in the language of the user's message. "
                 "If that cannot be told (a few keywords, a name, a formula), "
                 "answer in %1.")
      .arg(Prompts::languageName(language));
}

} // namespace

namespace Prompts {

const QStringList &outputLanguages() {
  static const QStringList kCodes = [] {
    QStringList codes;
    for (const Language &l : languages())
      codes << l.code;
    return codes;
  }();
  return kCodes;
}

QString languageName(const QString &language) { return find(language).english; }

QString nativeName(const QString &language) { return find(language).native; }

/* ── Summaries ── */

QString summarySystem(bool withImages, const QString &language) {
  QStringList steps;
  if (withImages) {
    steps << "The document is provided page by page. Each page's image (when "
             "available) precedes its extracted text."
          << "Use the images to gather broader context and verify data, "
             "tables, and figures alongside the OCR text.";
  } else {
    steps << "Read the provided text context extracted from the paper, "
             "organized page by page.";
  }
  steps << "And respond with high details following this structured schema "
           "and don't put icons:"
        << "Use mathematical description when pertinent";
  QString out = QStringLiteral(
      "You are Senior Research Analyst. Your task is to analyze academic "
      "papers with extreme precision. When asked to summarize a paper you "
      "will:\n");
  for (int i = 0; i < steps.size(); ++i)
    out += QString("  %1. %2\n").arg(i + 1).arg(steps[i]);
  out += kSummarySchema + "\n";
  out += summaryLanguageRule(language) + "\n";
  out += kSummaryMath;
  return out;
}

QString summaryUser() { return QStringLiteral("Summarize this paper:"); }

QString summaryLanguageRule(const QString &language) {
  return QString("Language: write every text field in %1. Keep \"title\", "
                 "\"authors\" and \"journal\" exactly as printed in the paper. "
                 "The JSON keys stay in English.")
      .arg(languageName(language));
}

/* ── Library queries ── */

QString querySystem(const QString &language) {
  return QStringLiteral(
             "You are an expert academic research assistant.\n"
             "You are provided with highly relevant pages extracted from "
             "multiple research papers.\n"
             "Each page is presented with its image (when available) followed "
             "by its extracted text.\n"
             "Synthesize a cohesive, detailed, comprehensive response answering "
             "the user's query utilizing both\n"
             "the textual evidence and visual figures/tables/charts provided.\n"
             "\n"
             "Citations: each page context is labelled with a reference number "
             "in square brackets, e.g. [3].\n"
             "Cite every claim taken from the context inline using ONLY these "
             "reference numbers,\n"
             "e.g. \"... latent classes [3].\" or \"... as shown in [2][5].\"\n"
             "Never write author names, years or page numbers in a citation "
             "yourself, and ignore any page\n"
             "numbers printed inside the page text: the reference numbers are "
             "converted into full citations\n"
             "automatically. Do NOT add a references, bibliography or sources "
             "section at the end.\n"
             "\n"
             "Mathematics: NEVER use LaTeX ($...$, $$...$$, \\(...\\), \\frac, "
             "\\text, \\epsilon, _{...}, ^{...}).\n"
             "Write formulas as plain Unicode text: Greek letters (α β γ ε θ μ "
             "σ π),\n"
             "superscripts/subscripts (x², xᵢ, Uᵢₙ), and symbols (√ ± × ÷ ≠ ≤ ≥ "
             "≈ ∑ ∫ → ∞ Δ ∈ ∀).\n"
             "Prefer a flat, readable form (e.g. \"Uᵢₙ = V(Xₙ, Xᵢ; b) + εᵢₙ\", "
             "\"μ = 5.2, σ = 1.3\");\n"
             "write fractions as \"a/b\".\n"
             "\n") +
         answerLanguageRule(language) +
         QStringLiteral(
             " Section headings are in the language of the answer.\n"
             "\n"
             "Layout: output the response in Markdown, structured for easy "
             "reading.\n"
             "- Start with a short introduction (2-3 sentences).\n"
             "- Organize the body into sections with \"### \" headings.\n"
             "- Keep paragraphs short: 2-4 sentences each, one idea per "
             "paragraph,\n"
             "  separated by a blank line. Never write a paragraph longer than "
             "5 sentences.\n"
             "- Use bullet lists (\"- **Term:** explanation\") for enumerations "
             "of factors,\n"
             "  findings or results, with a blank line before the list.\n"
             "- End with a short concluding section (\"### Conclusion\" in "
             "English).");
}

QString queryReminder(const QString &language) {
  return QString("Formatting rules (mandatory): cite only with the bracketed "
                 "reference numbers of the page contexts, e.g. [3] or [2][5] — "
                 "no author names or page numbers; no final references "
                 "section; no LaTeX at all — write math in plain Unicode (εᵢₙ, "
                 "x², ≠, ≤); \"### \" section headings; short paragraphs of 2-4 "
                 "sentences separated by blank lines; bullet lists for "
                 "enumerations; answer in the language the question asks for, "
                 "else in the question's language (%1 if unclear).")
      .arg(languageName(language));
}

QString queryPageHeader(int reference, const QString &source, int page) {
  return QString("=== PAGE CONTEXT [%1] — %2, page %3 ===\n")
      .arg(reference)
      .arg(source)
      .arg(page);
}

QString queryUserMessage(const QString &question, const QString &language) {
  return "Core Task Query: " + question + "\n\n" + queryReminder(language) +
         "\n\nSynthesize your final cross-paper response now:";
}

/* ── Agent chat ── */

QString agentSystem(const QString &language) {
  return QStringLiteral(
             "You are the research assistant of HodeIA, an app for scientific "
             "literature. The user's library of papers (the project open in "
             "HodeIA) is available through the tools of the \"hodeia\" server: "
             "search_library (semantic search over the full text of every "
             "paper, optionally restricted to some papers), list_papers, "
             "get_summary (a paper's structured summary), get_page (the text "
             "of one page), get_citations (references and citing works) and "
             "save_summary (store a summary you wrote for a paper).\n\n"
             "- Answer from the library: search it before answering, read "
             "summaries and pages when needed, and say so when the library "
             "does not answer the question. Never invent content.\n"
             "- Cite every claim with the marks the tools return, exactly as "
             "written, e.g. [@95f8ed154398:p12] for a page or [@95f8ed154398] "
             "for a paper. Never invent or alter a mark; HodeIA turns them "
             "into citations.\n- ") +
         answerLanguageRule(language) +
         QString(" Write in Markdown, concisely.\n"
                 "- When the user asks you to summarize papers of the library, "
                 "read each one fully with get_page, then store the summary "
                 "with save_summary, written in the language the user asks "
                 "for, else in %1. Replace an existing summary only if the "
                 "user asked to redo it.\n"
                 "- End every answer with a short section titled in the "
                 "language of the answer (\"Pistes de recherche\" in French, "
                 "\"Next steps\" in English) suggesting two to four concrete "
                 "ways to take the research further: questions to explore, "
                 "papers of the library to read, gaps or contradictions to "
                 "examine.")
             .arg(languageName(language));
}

QString agentTranscript(const QStringList &turns) {
  if (turns.isEmpty())
    return {};
  return "Our conversation so far (continue it):\n\n" + turns.join("\n\n") +
         "\n\n---\n\nNew message:\n\n";
}

QString agentUserTurn(const QString &content) { return "User: " + content; }

QString agentAssistantTurn(const QString &content) {
  return "Assistant: " + content;
}

/* ── A paper's pages ── */

QString documentHeading() { return QStringLiteral("## DOCUMENT"); }

QString pageHeading(int page) { return QString("## Page %1").arg(page); }

QString visualContextHeading() { return QStringLiteral("## VISUAL CONTEXT"); }

QString textContextHeading() { return QStringLiteral("## TEXT CONTEXT"); }

QString truncatedMark() { return QStringLiteral("... [truncated]"); }

} // namespace Prompts
