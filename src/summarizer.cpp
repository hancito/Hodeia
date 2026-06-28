#include "headers/summarizer.h"
#include "headers/batchworker.h"
#include "headers/configmanager.h"
#include "headers/database.h"
#include "headers/localbackend.h"
#include "headers/remotebackend.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QVariantMap>

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
  m_worker = new BatchWorker(database, this);
  m_workerThread = new QThread(this);

  m_localBackend = new LocalBackend();
  m_localBackend->moveToThread(m_workerThread);

  m_remoteBackend = new RemoteBackend();
  m_remoteBackend->moveToThread(m_workerThread);

  // Auto-check the active backend when it changes.
  QObject::connect(m_config, &ConfigManager::activeBackendChanged, this,
                   &Summarizer::loadActiveBackendModels);

  // Reload local model list after a model file is copied.
  QObject::connect(m_config, &ConfigManager::modelCopied, this,
                   &Summarizer::loadActiveBackendModels);

  // Initial load for the currently active backend.
  loadActiveBackendModels();

  m_workerThread->start();
}

Summarizer::~Summarizer() {
  if (m_workerThread->isRunning()) {
    m_workerThread->quit();
    m_workerThread->wait();
  }
}

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
  emit busyChanged();

  for (const QString &hash : hashes) {
    emit progressUpdated(hash, "Retrieving context...");
    QString context = m_worker->retrieveContext(hash);
    if (context.isEmpty()) {
      emit paperFinished(hash, false, "No context found");
      continue;
    }

    emit paperFinished(hash, true,
                       "Context retrieved (" +
                           QString::number(context.length()) + " chars)");
  }

  m_isBusy = false;
  emit busyChanged();
  emit allFinished(hashes.size(), hashes.size());
}

void Summarizer::cancel() {
  // TODO: forward cancel to the current BatchWorker
}

// ── Model discovery ──────────────────────────────────────────────────

void Summarizer::loadActiveBackendModels() {
  if (!m_config)
    return;

  QVariantMap cfg = m_config->backendConfig(m_config->activeBackend());
  QString name = cfg.value("name").toString();
  QString url = cfg.value("url").toString();
  QString key = cfg.value("api_key").toString();

  if (url.isEmpty()) {
    // Local backend — list .gguf files in Summarizers folder
    QStringList models = listLocalModels();
    emit modelsChecked(!models.isEmpty(), models, QString());
  } else {
    // Remote backend — fetch via HTTP
    checkModels(name, url, key);
  }
}

void Summarizer::checkModels(const QString &name, const QString &apiUrl,
                             const QString &apiKey) {
  if (!m_remoteBackend || apiUrl.isEmpty()) {
    emit modelsChecked(false, QStringList(), "Invalid parameters");
    return;
  }

  // One-shot connection: when the backend finishes, save or report error.
  QObject::connect(
      m_remoteBackend, &RemoteBackend::modelsReady, this,
      [this, name, apiUrl, apiKey](const QStringList &models,
                                   const QString &error) {
        if (error.isEmpty() && m_config) {
          m_config->setBackend(name, apiUrl, apiKey);
          // Only set active if it's different — avoids triggering
          // activeBackendChanged and a re-entrant checkModels loop.
          if (m_config->activeBackend() != name) {
            m_config->setActiveBackend(name);
          }
        }
        emit modelsChecked(error.isEmpty(), models, error);
      },
      Qt::SingleShotConnection);

  // Trigger fetchModels in the worker thread.
  QMetaObject::invokeMethod(
      m_remoteBackend,
      [this, apiUrl, apiKey]() {
        m_remoteBackend->fetchModels(apiUrl, apiKey);
      },
      Qt::QueuedConnection);
}

// ── Model discovery ──────────────────────────────────────────────────

QStringList Summarizer::listLocalModels() const {
  QString summarizersDir = m_config ? m_config->summarizersPath() : QString();
  QDir dir(summarizersDir);
  if (!dir.exists())
    return QStringList();

  QStringList filters;
  filters << "*.gguf";
  dir.setNameFilters(filters);
  dir.setFilter(QDir::Files | QDir::NoSymLinks);

  QStringList models;
  for (const QString &file : dir.entryList(filters)) {
    models.append(file);
  }
  return models;
}
