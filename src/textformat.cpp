#include "textformat.h"

#include <QHash>
#include <QRegularExpression>

#include <functional>

namespace {

using MatchFn = std::function<QString(const QRegularExpressionMatch &)>;

/// Replace every match of @p re in @p s with fn(match). Returns true if any
/// replacement happened.
bool replaceAll(QString &s, const QRegularExpression &re, const MatchFn &fn) {
  QString out;
  qsizetype last = 0;
  bool changed = false;
  auto it = re.globalMatch(s);
  while (it.hasNext()) {
    const auto mt = it.next();
    out += s.mid(last, mt.capturedStart() - last);
    out += fn(mt);
    last = mt.capturedEnd();
    changed = true;
  }
  if (changed) {
    out += s.mid(last);
    s = out;
  }
  return changed;
}

QHash<QChar, QChar> buildMap(const QString &from, const QString &to) {
  QHash<QChar, QChar> m;
  for (qsizetype i = 0; i < from.size() && i < to.size(); ++i)
    m.insert(from[i], to[i]);
  return m;
}

/// Convert a subscript/superscript body to Unicode script characters, or to
/// a flat "_x" / "^(…)" notation when some character has no script form.
QString toScript(const QString &content, bool super) {
  static const QHash<QChar, QChar> supMap = [] {
    auto m = buildMap(
        QStringLiteral("0123456789+-=()niTabcdefghjklmoprstuvwxyz′"),
        QStringLiteral("⁰¹²³⁴⁵⁶⁷⁸⁹⁺⁻⁼⁽⁾ⁿⁱᵀᵃᵇᶜᵈᵉᶠᵍʰʲᵏˡᵐᵒᵖʳˢᵗᵘᵛʷˣʸᶻ′"));
    m.insert(QChar(0x2212), QChar(0x207B)); // − → ⁻
    return m;
  }();
  static const QHash<QChar, QChar> subMap = [] {
    auto m = buildMap(QStringLiteral("0123456789+-=()aehijklmnoprstuvx"),
                      QStringLiteral("₀₁₂₃₄₅₆₇₈₉₊₋₌₍₎ₐₑₕᵢⱼₖₗₘₙₒₚᵣₛₜᵤᵥₓ"));
    m.insert(QChar(0x2212), QChar(0x208B)); // − → ₋
    return m;
  }();
  const QHash<QChar, QChar> &map = super ? supMap : subMap;

  const QString trimmed = content.trimmed();
  QString out;
  bool allMapped = !trimmed.isEmpty();
  for (const QChar ch : trimmed) {
    const auto it = map.constFind(ch);
    if (it == map.constEnd()) {
      allMapped = false;
      break;
    }
    out += it.value();
  }
  if (allMapped)
    return out;

  const QString marker = super ? QStringLiteral("^") : QStringLiteral("_");
  static const QRegularExpression simple(QStringLiteral("^\\w+$"));
  return simple.match(trimmed).hasMatch() ? marker + trimmed
                                          : marker + "(" + trimmed + ")";
}

/// Parenthesise a fraction operand unless it is a single token.
QString wrapIfComplex(const QString &s) {
  const QString t = s.trimmed();
  static const QRegularExpression complex(QStringLiteral("[\\s+\\-−=,/]"));
  return complex.match(t).hasMatch() ? "(" + t + ")" : t;
}

/// Convert the body of one math expression (delimiters already removed).
QString convertMath(QString m, TextFormat::Target target) {
  static const QHash<QString, QString> symbols = {
      // Greek
      {"alpha", "α"}, {"beta", "β"}, {"gamma", "γ"}, {"delta", "δ"},
      {"epsilon", "ε"}, {"varepsilon", "ε"}, {"zeta", "ζ"}, {"eta", "η"},
      {"theta", "θ"}, {"vartheta", "ϑ"}, {"iota", "ι"}, {"kappa", "κ"},
      {"lambda", "λ"}, {"mu", "μ"}, {"nu", "ν"}, {"xi", "ξ"}, {"pi", "π"},
      {"rho", "ρ"}, {"sigma", "σ"}, {"tau", "τ"}, {"upsilon", "υ"},
      {"phi", "φ"}, {"varphi", "φ"}, {"chi", "χ"}, {"psi", "ψ"},
      {"omega", "ω"}, {"Gamma", "Γ"}, {"Delta", "Δ"}, {"Theta", "Θ"},
      {"Lambda", "Λ"}, {"Xi", "Ξ"}, {"Pi", "Π"}, {"Sigma", "Σ"},
      {"Phi", "Φ"}, {"Psi", "Ψ"}, {"Omega", "Ω"},
      // Relations & operators
      {"neq", "≠"}, {"ne", "≠"}, {"leq", "≤"}, {"le", "≤"}, {"geq", "≥"},
      {"ge", "≥"}, {"approx", "≈"}, {"sim", "∼"}, {"simeq", "≃"},
      {"equiv", "≡"}, {"propto", "∝"}, {"times", "×"}, {"cdot", "·"},
      {"div", "÷"}, {"pm", "±"}, {"mp", "∓"}, {"infty", "∞"}, {"sum", "∑"},
      {"prod", "∏"}, {"int", "∫"}, {"partial", "∂"}, {"nabla", "∇"},
      {"to", "→"}, {"rightarrow", "→"}, {"leftarrow", "←"},
      {"Rightarrow", "⇒"}, {"Leftarrow", "⇐"}, {"iff", "⇔"},
      {"Leftrightarrow", "⇔"}, {"mapsto", "↦"}, {"in", "∈"},
      {"notin", "∉"}, {"subset", "⊂"}, {"subseteq", "⊆"}, {"cup", "∪"},
      {"cap", "∩"}, {"emptyset", "∅"}, {"forall", "∀"}, {"exists", "∃"},
      {"neg", "¬"}, {"land", "∧"}, {"lor", "∨"}, {"succ", "≻"},
      {"prec", "≺"}, {"succeq", "⪰"}, {"preceq", "⪯"}, {"mid", "|"},
      {"prime", "′"}, {"ldots", "…"}, {"cdots", "⋯"}, {"dots", "…"},
      {"langle", "⟨"}, {"rangle", "⟩"}, {"lfloor", "⌊"}, {"rfloor", "⌋"},
      {"lceil", "⌈"}, {"rceil", "⌉"},
      // Decorations / sizing / spacing: dropped (their argument is kept)
      {"hat", ""}, {"bar", ""}, {"tilde", ""}, {"vec", ""}, {"left", ""},
      {"right", ""}, {"big", ""}, {"Big", ""}, {"bigg", ""}, {"Bigg", ""},
      {"displaystyle", ""}, {"quad", " "}, {"qquad", " "},
  };
  static const QHash<QChar, QString> blackboard = {
      {'R', "ℝ"}, {'N', "ℕ"}, {'Z', "ℤ"}, {'Q', "ℚ"},
      {'C', "ℂ"}, {'E', "𝔼"}, {'P', "ℙ"}};

  // Line breaks, spacing commands and escaped delimiters.
  m.replace(QStringLiteral("\\\\"), QStringLiteral(" "));
  static const QRegularExpression spacing(QStringLiteral("\\\\[,;:! ]"));
  m.replace(spacing, QStringLiteral(" "));
  m.replace(QStringLiteral("\\{"), QStringLiteral("{"))
      .replace(QStringLiteral("\\}"), QStringLiteral("}"))
      .replace(QStringLiteral("\\|"), QStringLiteral("‖"))
      .replace(QStringLiteral("\\%"), QStringLiteral("%"));

  // Known symbol commands; brace-taking commands are handled below.
  static const QRegularExpression cmd(QStringLiteral("\\\\([A-Za-z]+)"));
  replaceAll(m, cmd, [](const QRegularExpressionMatch &mt) {
    const auto sym = symbols.constFind(mt.captured(1));
    return sym != symbols.constEnd() ? sym.value() : mt.captured(0);
  });

  // Brace-taking constructs, innermost first, until nothing changes.
  static const QRegularExpression textLike(QStringLiteral(
      "\\\\(?:text|textrm|textit|textbf|mathrm|mathit|mathbf|mathsf|mathcal|"
      "mathscr|boldsymbol|bm|operatorname)\\s*\\{([^{}]*)\\}"));
  static const QRegularExpression mathbb(
      QStringLiteral("\\\\mathbb\\s*\\{([^{}]*)\\}"));
  static const QRegularExpression frac(QStringLiteral(
      "\\\\[dt]?frac\\s*\\{([^{}]*)\\}\\s*\\{([^{}]*)\\}"));
  static const QRegularExpression sqrt(
      QStringLiteral("\\\\sqrt\\s*\\{([^{}]*)\\}"));
  static const QRegularExpression scriptBrace(
      QStringLiteral("([_^])\\s*\\{([^{}]*)\\}"));
  static const QRegularExpression bareGroup(QStringLiteral("\\{([^{}]*)\\}"));

  for (int guard = 0; guard < 32; ++guard) {
    bool changed = false;
    changed |= replaceAll(m, textLike, [](const QRegularExpressionMatch &mt) {
      return mt.captured(1);
    });
    changed |= replaceAll(m, mathbb, [](const QRegularExpressionMatch &mt) {
      const QString c = mt.captured(1).trimmed();
      return (c.size() == 1 && blackboard.contains(c[0])) ? blackboard[c[0]]
                                                          : c;
    });
    changed |= replaceAll(m, frac, [](const QRegularExpressionMatch &mt) {
      return wrapIfComplex(mt.captured(1)) + "/" +
             wrapIfComplex(mt.captured(2));
    });
    changed |= replaceAll(m, sqrt, [](const QRegularExpressionMatch &mt) {
      return "√" + wrapIfComplex(mt.captured(1));
    });
    changed |= replaceAll(m, scriptBrace, [](const QRegularExpressionMatch &mt) {
      return toScript(mt.captured(2), mt.captured(1) == "^");
    });
    // Strip plain grouping braces only once nothing more specific applies.
    if (!changed)
      changed = replaceAll(m, bareGroup, [](const QRegularExpressionMatch &mt) {
        return mt.captured(1);
      });
    if (!changed)
      break;
  }

  // Single-character scripts: x_i, x^2, x^′.
  static const QRegularExpression scriptChar(
      QStringLiteral("([_^])\\s*([A-Za-z0-9′+\\-])"));
  replaceAll(m, scriptChar, [](const QRegularExpressionMatch &mt) {
    return toScript(mt.captured(2), mt.captured(1) == "^");
  });

  // Unknown commands (\log, \max, \Pr…): keep the name, drop the backslash.
  m.replace(cmd, QStringLiteral("\\1"));

  if (target == TextFormat::Target::Markdown)
    m.replace(QStringLiteral("*"), QStringLiteral("\\*"))
        .replace(QStringLiteral("_"), QStringLiteral("\\_"));
  return m.simplified();
}

/// Convert every math span in a segment that contains no code.
QString convertSegment(const QString &seg, TextFormat::Target target) {
  static const QRegularExpression math(QStringLiteral(
      "\\$\\$([\\s\\S]+?)\\$\\$"           // 1: $$…$$
      "|\\\\\\[([\\s\\S]+?)\\\\\\]"         // 2: \[…\]
      "|\\\\\\((.+?)\\\\\\)"                // 3: \(…\)
      "|(?<![\\\\$\\w])\\$(?=\\S)([^$\\n]+?)(?<=\\S)\\$(?![\\d$])" // 4: $…$
      ));
  QString out = seg;
  replaceAll(out, math, [target](const QRegularExpressionMatch &mt) {
    QString body;
    for (int g = 1; g <= 4 && body.isEmpty(); ++g)
      body = mt.captured(g);
    return convertMath(body, target);
  });
  return out;
}

} // namespace

namespace TextFormat {

QString latexToUnicode(const QString &text, Target target) {
  if (target == Target::PlainText)
    return convertSegment(text, target);

  // Markdown: leave fenced and inline code untouched.
  static const QRegularExpression code(
      QStringLiteral("```[\\s\\S]*?```|`[^`\\n]*`"));
  QString out;
  qsizetype last = 0;
  auto it = code.globalMatch(text);
  while (it.hasNext()) {
    const auto mt = it.next();
    out += convertSegment(text.mid(last, mt.capturedStart() - last), target);
    out += mt.captured(0);
    last = mt.capturedEnd();
  }
  out += convertSegment(text.mid(last), target);
  return out;
}

QString escapeMarkdown(const QString &text) {
  static const QString specials = QStringLiteral("\\`*_[]<>#|");
  QString out;
  out.reserve(text.size());
  for (const QChar ch : text) {
    if (specials.contains(ch))
      out += QLatin1Char('\\');
    out += ch;
  }
  return out;
}

} // namespace TextFormat
