#include "headers/summarizer.h"
#include "headers/configmanager.h"
#include "headers/database.h"
#include "headers/llamacppbackend.h"
#include "headers/openaiserverbackend.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

// ── System prompt ────────────────────────────────────────────────────
static const QString s_systemPrompt = []() {
  return R"(You are an expert academic paper summarizer.
Given the text of a research paper, produce a structured summary in the following JSON format:
{
  "title": "paper title",
  "authors": ["author1", "author2"],
  "journal": "journal or conference name",
  "year": 2024,
  "topics": ["topic1", "topic2"],
  "researchObjective": "...",
  "methodology": "...",
  "coreFindingsAndSignificance": "...",
  "criticalLimitations": "...",
  "citableSummary": "..."
}
Return only valid JSON.)";
}();

const QString &Summarizer::systemPrompt() { return s_systemPrompt; }

// ── Construction / destruction ───────────────────────────────────────
Summarizer::Summarizer(ConfigManager *config, DatabaseManager *database,
                       QObject *parent)
    : QObject(parent), m_config(config), m_database(database) {
  m_workerThread = new QThread(this);

  m_localBackend = new LlamaCppBackend();
  m_localBackend->moveToThread(m_workerThread);

  m_remoteBackend = new OpenAiServerBackend();
  m_remoteBackend->moveToThread(m_workerThread);

  m_workerThread->start();
}

Summarizer::~Summarizer() {
  cancel();
  m_workerThread->quit();
  m_workerThread->wait();
}

// ── Public API ───────────────────────────────────────────────────────
void Summarizer::summarizePapers(const QStringList &hashes) {
  if (m_isBusy)
    return;
  m_hashes = hashes;
  m_cancelled = false;
  m_isBusy = true;
  emit busyChanged();

  QMetaObject::invokeMethod(this, "runBatch", Qt::QueuedConnection);
}

void Summarizer::cancel() { m_cancelled = true; }

// ── Worker-thread batch runner ───────────────────────────────────────
void Summarizer::runBatch() {
  int success = 0;
  int total = m_hashes.size();

  for (const QString &hash : m_hashes) {
    if (m_cancelled) {
      emit paperFinished(hash, false, "Cancelled");
      continue;
    }

    emit progressUpdated(hash, "Retrieving context...");
    QString context = retrieveContext(hash);
    if (context.isEmpty()) {
      emit paperFinished(hash, false, "No context found");
      continue;
    }

    emit progressUpdated(hash, "Building prompt...");
    QString userPrompt = buildUserPrompt(context);

    emit progressUpdated(hash, "Generating summary...");

    // TODO: select backend (local vs remote) based on config
    QString rawOutput = m_localBackend->generate(s_systemPrompt, userPrompt);

    if (rawOutput.isEmpty()) {
      emit paperFinished(hash, false, "LLM generation failed");
      continue;
    }

    PaperSummary summary = parseSummary(rawOutput);
    QByteArray jsonBytes = summaryToJson(summary);
    bool ok = upsertSummary(hash, jsonBytes, "llamacpp");

    if (ok) {
      success++;
      emit paperFinished(hash, true, "Summary saved");
    } else {
      emit paperFinished(hash, false, "Failed to save summary to DB");
    }
  }

  m_isBusy = false;
  emit busyChanged();
  emit allFinished(success, total);
}

// ── Context retrieval ────────────────────────────────────────────────
QString Summarizer::retrieveContext(const QString &paperHash) const {
  // TODO: query chunks table ordered by page number, concatenate text
  QString text;
  Q_UNUSED(paperHash)
  Q_UNUSED(text)
  return text;
}

QString Summarizer::buildUserPrompt(const QString &textContext) const {
  return textContext.left(8000); // truncate to fit context window
}

// ── Parsing ──────────────────────────────────────────────────────────
PaperSummary Summarizer::parseSummary(const QString &rawOutput) const {
  PaperSummary s;

  QJsonDocument doc = QJsonDocument::fromJson(rawOutput.toUtf8());
  if (!doc.isObject())
    return s;

  QJsonObject obj = doc.object();
  s.title = obj.value("title").toString();
  s.journal = obj.value("journal").toString();
  s.year = obj.value("year").toInt();
  s.researchObjective = obj.value("researchObjective").toString();
  s.methodology = obj.value("methodology").toString();
  s.coreFindingsAndSignificance =
      obj.value("coreFindingsAndSignificance").toString();
  s.criticalLimitations = obj.value("criticalLimitations").toString();
  s.citableSummary = obj.value("citableSummary").toString();

  QJsonArray authorsArr = obj.value("authors").toArray();
  for (const auto &a : authorsArr)
    s.authors << a.toString();

  QJsonArray topicsArr = obj.value("topics").toArray();
  for (const auto &t : topicsArr)
    s.topics << t.toString();

  return s;
}

QByteArray Summarizer::summaryToJson(const PaperSummary &s) const {
  QJsonObject obj;
  obj["title"] = s.title;
  obj["journal"] = s.journal;
  obj["year"] = s.year;
  obj["researchObjective"] = s.researchObjective;
  obj["methodology"] = s.methodology;
  obj["coreFindingsAndSignificance"] = s.coreFindingsAndSignificance;
  obj["criticalLimitations"] = s.criticalLimitations;
  obj["citableSummary"] = s.citableSummary;

  QJsonArray authorsArr;
  for (const auto &a : s.authors)
    authorsArr.append(a);
  obj["authors"] = authorsArr;

  QJsonArray topicsArr;
  for (const auto &t : s.topics)
    topicsArr.append(t);
  obj["topics"] = topicsArr;

  return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

// ── Database ─────────────────────────────────────────────────────────
bool Summarizer::upsertSummary(const QString &paperHash,
                               const QByteArray &jsonBytes,
                               const QString &modelName) {
  // TODO: execute UPSERT on Summaries table via m_database
  Q_UNUSED(paperHash)
  Q_UNUSED(jsonBytes)
  Q_UNUSED(modelName)
  return false;
}
