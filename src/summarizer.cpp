#include "headers/summarizer.h"
#include "headers/configmanager.h"
#include "headers/database.h"
#include "headers/localbackend.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QVariantMap>

// ── System prompt ────────────────────────────────────────────────────
static const QString s_systemPrompt = []() {
  return R"(You are Senior Research Analyst. Your task is to analyze academic papers with extreme precision. When asked to summarize a paper yo will:
  1. Read the provided text context
  2. Look at the provided images of the document to gather broader context and verify data/tables.
  3. And respond with high details following this structured schema in english and don't put icons:
{
  "title": "The full title of the paper",
  "authors": [A list of author names],
  "journal": "journal or conference name",
  "year": "The publication year as an integer",
  "topics": [A list the 5 most relevant topics or keywords from the paper.],
  "researchObjective": "Identify the primary question or hypothesis in 1 sentence",
  "methodology": "Describe the study design, sample size, and key variables. Use a paragraph",
  "coreFindingsAndSignificance": "Highlight the most statistically or theoretically significant results. Use a bulleted list",
  "criticalLimitations": "Identify a list of internal or external validity threats mentioned or observed.",
  "citableSummary": "A two-sentence summary suitable for a literature review"
  "references": "A list of the references cited by the paper"
}
Use latex format for mathematics. Don't use markdown formating. Return only valid JSON.)";
}();

const QString &Summarizer::systemPrompt() { return s_systemPrompt; }

// ── Construction / destruction ───────────────────────────────────────

Summarizer::Summarizer(ConfigManager *config, DatabaseManager *database,
                       QObject *parent)
    : QObject(parent), m_config(config), m_database(database) {}

Summarizer::~Summarizer() {}

// ── Public API ───────────────────────────────────────────────────────

void Summarizer::summarizePapers(const QStringList &hashes) {
  if (m_isBusy || !m_config || hashes.isEmpty()) {
    if (hashes.isEmpty())
      qDebug() << "[Summarizer] summarizePapers — no hashes";
    if (m_isBusy)
      qDebug() << "[Summarizer] summarizePapers — already busy";
    return;
  }

  // Capture backend configuration from the main thread.
  QString backendName = m_config->activeBackend();
  QVariantMap backendCfg = m_config->backendConfig(backendName);
  QString backendUrl = backendCfg.value("url").toString();
  QString backendKey = backendCfg.value("api_key").toString();
  QString modelPath = m_config->activeModel();

  // For local backend, resolve to full path now (from main thread).
  if (backendName == "Llama.cpp" && !modelPath.isEmpty()) {
    QString ggufPath = m_config->summarizersPath() + "/" + modelPath;
    if (QFile::exists(ggufPath)) {
      modelPath = ggufPath;
    }
  }

  qDebug() << "[Summarizer] summarizePapers — papers:" << hashes.size()
           << "backend:" << backendName << "model:" << modelPath;

  m_isBusy = true;

  for (const QString &hash : hashes) {
    // emit progressUpdated(hash, "Retrieving context...");
    QString context = retrieveContext(hash);
    if (context.isEmpty()) {
      //  emit paperFinished(hash, false, "No context found");
      continue;
    }

    // emit paperFinished(hash, true,
    //                  "Context retrieved (" +
    //                    QString::number(context.length()) + " chars)");
  }

  m_isBusy = false;
  // emit allFinished(hashes.size(), hashes.size());
}

QString Summarizer::retrieveContext(const QString &paperHash) const {
  if (!m_database || !m_database->isOpen()) {
    qWarning() << "[Summarizer] database not open";
    return QString();
  }

  auto &db = m_database->db();
  auto &vecMgr = m_database->vecManager();

  QList<ChunkSearchResult> chunks = vecMgr.getChunksByHash(db, paperHash);
  if (chunks.isEmpty()) {
    qDebug() << "[Summarizer] no chunks for hash:" << paperHash;
    return QString();
  }

  QString context;
  for (const auto &c : chunks) {
    context += QString("Page %1: %2\n\n").arg(c.page).arg(c.text);
  }

  qDebug() << "[Summarizer] retrieved" << chunks.size()
           << "chunks, length:" << context.length() << "for hash:" << paperHash;
  return context;
}
