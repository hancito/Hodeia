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

ModelHelper::~ModelHelper() {
  if (m_workerThread && m_workerThread->isRunning()) {
    m_workerThread->quit();
    m_workerThread->wait(3000);
  }
}

QStringList ModelHelper::listLocalModels() {
  if (!m_config)
    return QStringList();

  QString summarizersDir = m_config->summarizersPath();
  QDir dir(summarizersDir);
  if (!dir.exists())
    return QStringList();

  // Each model lives in its own subfolder (e.g. Summarizers/<model>/).
  // Scan subfolders and return the main .gguf path inside each.
  QStringList allDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  QStringList models;

  for (const QString &subdir : allDirs) {
    QDir subDir(summarizersDir + "/" + subdir);
    QStringList ggufFiles =
        subDir.entryList(QStringList{"*.gguf"}, QDir::Files | QDir::NoSymLinks);
    // Pick the first non-mmproj .gguf as the main model.
    for (const QString &f : ggufFiles) {
      if (!f.contains("mmproj", Qt::CaseInsensitive)) {
        models.append(subdir + "/" + f);
        break;
      }
    }
  }

  return models;
}

QStringList ModelHelper::getImagesForHash(const QString &paperHash,
                                          const QString &imgPath) const {
  QDir imgDir(imgPath);
  if (!imgDir.exists()) {
    qWarning() << "Image directory does not exist:" << imgPath;
    return QStringList();
  }

  imgDir.setNameFilters({paperHash + "_page_*.png"});
  const QFileInfoList matchingFiles = imgDir.entryInfoList();

  QStringList paths;
  paths.reserve(matchingFiles.size());
  for (const QFileInfo &fi : matchingFiles) {
    paths.append(fi.absoluteFilePath());
  }
  paths.sort();

  return paths;
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
