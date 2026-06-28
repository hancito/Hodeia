#include "headers/helpers.h"
#include "headers/configmanager.h"
#include "headers/remotebackend.h"

#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QThread>

ModelHelper::ModelHelper(ConfigManager *config, QObject *parent)
    : QObject(parent), m_config(config) {
  m_workerThread = new QThread(this);

  m_remoteBackend = new RemoteBackend();
  m_remoteBackend->moveToThread(m_workerThread);

  // Auto-reload when the active backend changes.
  QObject::connect(m_config, &ConfigManager::activeBackendChanged, this,
                   &ModelHelper::loadActiveBackendModels);

  // Reload local model list after a model file is copied.
  QObject::connect(m_config, &ConfigManager::modelCopied, this,
                   &ModelHelper::loadActiveBackendModels);

  // Initial load for the currently active backend.
  loadActiveBackendModels();

  m_workerThread->start();
}

QStringList ModelHelper::listLocalModels() {
  if (!m_config)
    return QStringList();

  QString summarizersDir = m_config->summarizersPath();
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

void ModelHelper::checkRemoteModels(const QString &name, const QString &apiUrl,
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
          // activeBackendChanged and a re-entrant check loop.
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

void ModelHelper::loadActiveBackendModels() {
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
    checkRemoteModels(name, url, key);
  }
}
