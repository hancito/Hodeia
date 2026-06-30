#include "headers/summarizer.h"
#include "headers/configmanager.h"
#include "headers/database.h"
#include "headers/llama_chat_backend.h"
#include "headers/remotebackend.h"
#include "headers/sqlitevec.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QThread>
#include <QVariantMap>
#include <QVector>

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

static const QString s_userPrompt = []() {
  return R"(Summarize this paper:)";
}();

const QString &Summarizer::systemPrompt() { return s_systemPrompt; }
const QString &Summarizer::userPrompt() { return s_userPrompt; }

// ── Construction / destruction ───────────────────────────────────────

Summarizer::Summarizer(ConfigManager *config, DatabaseManager *database,
                       QObject *parent)
    : QObject(parent), m_config(config), m_database(database) {
  m_chatBackend = new LlamaChatBackend(this);
  m_remoteBackend = new RemoteBackend(this);
}

Summarizer::~Summarizer() {}

// ── Public API ───────────────────────────────────────────────────────

void Summarizer::summarizePapers(const QStringList &hashes,
                                 ConfigManager *config) {
  emit started();
  if (hashes.isEmpty()) {
    qDebug() << "[Summarizer] summarizePapers — no hashes";
    return;
  }

  QElapsedTimer totalTimer;
  totalTimer.start();

  qDebug() << "[Summarizer] Starting summarization for" << hashes.size()
           << "paper(s)";

  // Resolve the active backend from the passed ConfigManager.
  QString backendName = config->activeBackend();
  QString activeModel = config->activeModel();
  QVariantMap backendCfg = config->backendConfig(backendName);
  QString backendUrl = backendCfg.value("url").toString();

  qDebug() << "[Summarizer] Backend:" << backendName
           << "| Model:" << activeModel;

  bool wasEmbeddingReady = false;
  bool isLocalBackend = (backendName == "Llama.cpp");

  // Free GPU memory by unloading the embedding model before loading the
  // (much larger) chat model.
  if (isLocalBackend && m_database) {
    SqliteVecManager &vecMgr = m_database->vecManager();
    if (vecMgr.isReady()) {
      wasEmbeddingReady = true;
      vecMgr.shutdown();
      qDebug() << "[Summarizer] Embedding model unloaded to free GPU memory";
    }
  }

  int successCount = 0;

  for (int i = 0; i < hashes.size(); ++i) {
    const QString &hash = hashes[i];
    qDebug() << "[Summarizer] Processing paper" << i + 1 << "/" << hashes.size()
             << "—" << hash;
    PromptPayload payload = buildPrompt(hash);
    qDebug() << "[Summarizer] Prompt built — text context:"
             << payload.textContext.length()
             << "chars, visuals:" << payload.visualContext.size();

    QString result;
    QString error;

    if (isLocalBackend) {
      // ── Local backend ──
      QString modelPath = activeModel;
      if (!modelPath.isEmpty()) {
        QString ggufPath = config->summarizersPath() + "/" + modelPath;
        if (QFile::exists(ggufPath)) {
          modelPath = ggufPath;
        }
      }
      qDebug() << "[Summarizer] Loading local model:" << modelPath;
      QElapsedTimer loadTimer;
      loadTimer.start();
      if (!m_chatBackend->loadModel(modelPath)) {
        error = "Failed to load local model";
        emit paperFinished(hash, false, error);
        continue;
      }
      qDebug() << "[Summarizer] Model loaded in" << loadTimer.elapsed() << "ms";

      QElapsedTimer genTimer;
      genTimer.start();
      result = m_chatBackend->summarize(payload);
      qDebug() << "[Summarizer] Generation completed in" << genTimer.elapsed()
               << "ms"
               << "(" << result.length() << "chars)";
    } else {
      // ── Remote backend ──
      QString apiUrl = backendCfg.value("url").toString();
      QString apiKey = backendCfg.value("api_key").toString();
      m_remoteBackend->setApiUrl(apiUrl);
      m_remoteBackend->setApiKey(apiKey);
      m_remoteBackend->setModelName(activeModel);

      qDebug() << "[Summarizer] Remote request — URL:" << apiUrl
               << "| Model:" << activeModel;

      QElapsedTimer genTimer;
      genTimer.start();

      // Synchronous wait for the async remote call.
      QEventLoop loop;
      QObject::connect(
          m_remoteBackend, &RemoteBackend::finished, this,
          [&result, &error, &loop](const QString &text, const QString &err) {
            result = text;
            error = err;
            loop.quit();
          },
          Qt::SingleShotConnection);

      m_remoteBackend->generate(payload.systemPrompt,
                                payload.userMessage + "\n\n" +
                                    payload.textContext,
                                payload.visualContext);
      loop.exec();

      qDebug() << "[Summarizer] Remote generation completed in"
               << genTimer.elapsed() << "ms"
               << "(" << result.length() << "chars)" << result;
    }

    if (error.isEmpty() && !result.isEmpty()) {
      successCount++;
      emit paperFinished(hash, true,
                         "Summary generated (" +
                             QString::number(result.length()) + " chars)");
      qDebug() << "[Summarizer] ✓ Paper" << hash << "summarized successfully"
               << "(" << result.length() << "chars)";
    } else {
      QString msg = error.isEmpty() ? "Empty response" : error;
      emit paperFinished(hash, false, msg);
      qDebug() << "[Summarizer] ✗ Paper" << hash << "failed:" << msg;
    }
  }

  emit allFinished(successCount, hashes.size());

  qDebug() << "[Summarizer] All done —" << successCount << "/" << hashes.size()
           << "succeeded in" << totalTimer.elapsed() << "ms";

  // Restore the embedding model after summarization is complete.
  if (wasEmbeddingReady && m_database) {
    qDebug() << "[Summarizer] Reloading embedding model...";
    QElapsedTimer embedTimer;
    embedTimer.start();
    m_database->vecManager().initialize();
    qDebug() << "[Summarizer] Embedding model reloaded in"
             << embedTimer.elapsed() << "ms";
  }

  // Unload the chat model to free GPU memory for future use.
  if (isLocalBackend) {
    m_chatBackend->unloadModel();
    qDebug() << "[Summarizer] Chat model unloaded";
  }
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

std::vector<PageImage>
Summarizer::retrieveVisualContext(const QString &paperHash) const {
  std::vector<PageImage> result;

  if (!m_config) {
    qWarning() << "[Summarizer] no ConfigManager";
    return result;
  }

  QString imgPath = m_config->projectImgPath(m_config->lastProject());
  QDir imgDir(imgPath);
  if (!imgDir.exists()) {
    qWarning() << "[Summarizer] Image directory does not exist:" << imgPath;
    return result;
  }

  // Match files like: 4ad90c343ca0d5a05e67f80da33a724b_page_001.png
  imgDir.setNameFilters({paperHash + "_page_*.png"});
  const QFileInfoList matchingFiles = imgDir.entryInfoList();

  // Sort by file name so page order is preserved
  QStringList paths;
  for (const QFileInfo &fileInfo : matchingFiles) {
    paths.append(fileInfo.absoluteFilePath());
  }
  paths.sort();

  for (const QString &path : paths) {
    QImage img(path);
    if (img.isNull()) {
      qWarning() << "[Summarizer] Failed to load image:" << path;
      continue;
    }
    QImage rgb = img.convertToFormat(QImage::Format_RGB888);
    PageImage pi;
    pi.width = rgb.width();
    pi.height = rgb.height();
    pi.data.assign(rgb.bits(), rgb.bits() + rgb.sizeInBytes());
    result.push_back(std::move(pi));
  }

  qDebug() << "[Summarizer] loaded" << result.size()
           << "page image(s) for hash:" << paperHash;
  return result;
}

PromptPayload Summarizer::buildPrompt(const QString &paperHash) const {
  PromptPayload payload;

  payload.systemPrompt = s_systemPrompt;
  payload.userMessage = s_userPrompt;
  payload.textContext = retrieveContext(paperHash);
  payload.visualContext = retrieveVisualContext(paperHash);

  return payload;
}

void Summarizer::stop() {
  // TODO: forward cancel to the current inference pipeline
}
