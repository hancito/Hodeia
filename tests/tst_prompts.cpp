// Unit tests for Prompts: the output language reaches every prompt, and
// what other code parses (JSON keys, citation tags, page headings) stays.

#include "prompts.h"
#include "summarypromptbuilder.h"

#include <QtTest>

class TestPrompts : public QObject {
  Q_OBJECT

private slots:
  void languages();
  void summary();
  void query();
  void agent();
  void pageLabels();
};

void TestPrompts::languages() {
  QCOMPARE(Prompts::outputLanguages().first(), Prompts::kDefaultLanguage);
  QVERIFY(Prompts::outputLanguages().contains("fr"));
  QCOMPARE(Prompts::languageName("fr"), QString("French"));
  QCOMPARE(Prompts::nativeName("fr"), QString("Français"));
  // Unknown: English.
  QCOMPARE(Prompts::languageName("xx"), QString("English"));
  for (const QString &code : Prompts::outputLanguages()) {
    QVERIFY(!Prompts::languageName(code).isEmpty());
    QVERIFY(!Prompts::nativeName(code).isEmpty());
  }
}

// The summary's text fields in the chosen language; its title, authors,
// journal and JSON keys unchanged.
void TestPrompts::summary() {
  for (bool images : {false, true}) {
    const QString p = Prompts::summarySystem(images, "fr");
    QVERIFY(p.contains("write every text field in French"));
    QVERIFY(p.contains("exactly as printed in the paper"));
    for (const char *key : {"\"title\"", "\"authors\"", "\"researchObjective\"",
                            "\"coreFindingsAndSignificance\"", "\"citableSummary\""})
      QVERIFY2(p.contains(key), key);
    QVERIFY(p.contains("Return only valid JSON."));
    QCOMPARE(p.contains("Each page's image"), images);
    QVERIFY(!p.contains("in english")); // the old fixed language is gone
  }
  QVERIFY(Prompts::summarySystem(false, "en").contains("in English."));
}

// Answers: the language the question asks for, else the question's, else
// the setting — in the system prompt and in the reminder at the end.
void TestPrompts::query() {
  const QString system = Prompts::querySystem("de");
  QVERIFY(system.contains("if the user says which language to answer in"));
  QVERIFY(system.contains("answer in German."));
  QVERIFY(system.contains("[3]")); // citation tags unchanged
  const QString user = Prompts::queryUserMessage("Pourquoi ?", "de");
  QVERIFY(user.startsWith("Core Task Query: Pourquoi ?\n\n"));
  QVERIFY(user.contains("question's language (German if unclear)"));
  QCOMPARE(Prompts::queryPageHeader(2, "Bull (1984)", 7),
           QString("=== PAGE CONTEXT [2] — Bull (1984), page 7 ===\n"));
}

void TestPrompts::agent() {
  const QString p = Prompts::agentSystem("es");
  QVERIFY(p.contains("answer in Spanish."));
  QVERIFY(p.contains("[@95f8ed154398:p12]")); // marks unchanged
  QVERIFY(p.contains("save_summary, written in the language the user asks for, "
                     "else in Spanish"));
  QCOMPARE(Prompts::agentTranscript({}), QString());
  QCOMPARE(Prompts::agentTranscript({Prompts::agentUserTurn("Hi"),
                                     Prompts::agentAssistantTurn("Hello")}),
           QString("Our conversation so far (continue it):\n\nUser: Hi\n\n"
                   "Assistant: Hello\n\n---\n\nNew message:\n\n"));
}

void TestPrompts::pageLabels() {
  QCOMPARE(Prompts::documentHeading(), QString("## DOCUMENT"));
  QCOMPARE(Prompts::pageHeading(3), QString("## Page 3"));
  // The summary builder uses the language it is given.
  const PromptPayload p = SummaryPrompt::build("h", {}, VisualMode::TextOnly,
                                               QString(), "it");
  QCOMPARE(p.userMessage, Prompts::summaryUser());
}

QTEST_GUILESS_MAIN(TestPrompts)
#include "tst_prompts.moc"
