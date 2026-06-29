#include "headers/configmanager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>

ConfigManager::ConfigManager(QObject *parent) : QObject(parent) {
  loadConfig(); // Automatically load the file when instantiated

  // If no backends were loaded, seed the default llama.cpp backend
  if (m_backendList.isEmpty()) {
    QVariantMap defaultBackend;
    defaultBackend["name"] = QStringLiteral("Llama.cpp");
    defaultBackend["url"] = QStringLiteral("");
    defaultBackend["api_key"] = QStringLiteral("");
    m_backendList.append(defaultBackend);
    m_activeBackend = QStringLiteral("Llama.cpp");
    saveConfig();
  }

  // Create "Default" project if no projects exist yet
  QDir projRoot(projectsRoot());
  if (!projRoot.exists() ||
      projRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
    createProject("Default");
  }
  ensureModelsDirectory();
  refreshProjectList();

  // Set up model copy worker thread
  m_copyThread = new QThread(this);
  m_copyWorker = new ModelCopyWorker();
  m_copyWorker->moveToThread(m_copyThread);
  QObject::connect(m_copyWorker, &ModelCopyWorker::progress, this,
                   &ConfigManager::modelCopyProgress);
  QObject::connect(
      m_copyWorker, &ModelCopyWorker::finished, this,
      [this](bool success, const QString &destPath, const QString &error) {
        emit modelCopyFinished(success, destPath, error);
        if (success) {
          emit modelCopied();
        }
      });
  m_copyThread->start();
}

ConfigManager::~ConfigManager() {
  saveConfig();
  if (m_copyThread) {
    m_copyThread->quit();
    m_copyThread->wait();
  }
}

QString ConfigManager::getFilePath() const {
  // Finds location like: AppData/Roaming/Paperize/ or ~/.local/share/Paperize/
  QString dirPath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dirPath); // Creates directory if it doesn't exist yet
  return dirPath + "/config.json";
}

void ConfigManager::loadConfig() {
  QFile file(getFilePath());
  if (!file.open(QIODevice::ReadOnly))
    return; // Use hardcoded defaults if file missing

  QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  if (doc.isObject()) {
    QJsonObject obj = doc.object();
    m_activeThemeIndex = obj.value("activeThemeIndex").toInt(0);
    m_lastProject = obj.value("lastProject").toString("");
    m_activeBackend = obj.value("activeBackend").toString("");
    m_activeModel = obj.value("activeModel").toString("");
    m_contextWindow =
        static_cast<unsigned int>(obj.value("contextWindow").toInt(4000));
    m_modelTemperature = obj.value("modelTemperature").toDouble(0.1);

    // Load backend list from JSON array
    // Expected format: [{"name":"..", "url":"..", "api_key":".."}, ...]
    QJsonArray backendArray = obj.value("backendList").toArray();
    m_backendList.clear();
    for (const auto &item : backendArray) {
      if (item.isObject()) {
        m_backendList.append(item.toObject().toVariantMap());
      }
    }
  }
}

void ConfigManager::saveConfig() {
  QFile file(getFilePath());
  if (!file.open(QIODevice::WriteOnly))
    return;

  QJsonObject obj;
  obj["activeThemeIndex"] = m_activeThemeIndex;
  obj["lastProject"] = m_lastProject;
  obj["activeBackend"] = m_activeBackend;
  obj["activeModel"] = m_activeModel;
  obj["contextWindow"] = static_cast<qint64>(m_contextWindow);
  obj["modelTemperature"] = m_modelTemperature;

  // Save backend list as JSON array of objects
  QJsonArray backendArray;
  for (const auto &entry : m_backendList) {
    QJsonObject backendObj;
    QVariantMap map = entry.toMap();
    backendObj["name"] = map.value("name").toString();
    backendObj["url"] = map.value("url").toString();
    backendObj["api_key"] = map.value("api_key").toString();
    backendArray.append(backendObj);
  }
  obj["backendList"] = backendArray;

  QJsonDocument doc(obj);
  file.write(doc.toJson());
}

// Setters save automatically to disk whenever a value changes
void ConfigManager::setActiveThemeIndex(int index) {
  if (m_activeThemeIndex != index) {
    m_activeThemeIndex = index;
    emit activeThemeIndexChanged();
    saveConfig();
  }
}

void ConfigManager::setLastProject(const QString &path) {
  if (m_lastProject != path) {
    m_lastProject = path;
    emit lastProjectChanged();
    saveConfig();
  }
}

void ConfigManager::setActiveBackend(const QString &backend) {
  if (m_activeBackend != backend) {
    m_activeBackend = backend;
    emit activeBackendChanged();
    saveConfig();
  }
}

void ConfigManager::setActiveModel(const QString &model) {
  if (m_activeModel != model) {
    m_activeModel = model;
    emit activeModelChanged();
    saveConfig();
  }
}

void ConfigManager::setContextWindow(unsigned int window) {
  if (m_contextWindow != window) {
    m_contextWindow = window;
    emit contextWindowChanged();
    saveConfig();
  }
}

void ConfigManager::setModelTemperature(double temperature) {
  if (m_modelTemperature != temperature) {
    m_modelTemperature = temperature;
    emit modelTemperatureChanged();
    saveConfig();
  }
}

void ConfigManager::setBackendList(const QVariantList &backends) {
  if (m_backendList != backends) {
    m_backendList = backends;
    emit backendListChanged();
    saveConfig();
  }
}

QVariantMap ConfigManager::backendConfig(const QString &name) const {
  for (const auto &entry : m_backendList) {
    QVariantMap map = entry.toMap();
    if (map.value("name").toString() == name) {
      return map;
    }
  }
  return QVariantMap();
}

QStringList ConfigManager::backendNames() const {
  QStringList names;
  for (const auto &entry : m_backendList) {
    names << entry.toMap().value("name").toString();
  }
  return names;
}

void ConfigManager::setBackend(const QString &name, const QString &url,
                               const QString &apiKey) {
  // Update existing entry
  for (int i = 0; i < m_backendList.size(); ++i) {
    QVariantMap map = m_backendList[i].toMap();
    if (map.value("name").toString() == name) {
      map["url"] = url;
      map["api_key"] = apiKey;
      m_backendList[i] = map;
      emit backendListChanged();
      saveConfig();
      return;
    }
  }
  // Add new entry
  QVariantMap newBackend;
  newBackend["name"] = name;
  newBackend["url"] = url;
  newBackend["api_key"] = apiKey;
  m_backendList.append(newBackend);
  emit backendListChanged();
  saveConfig();
}

void ConfigManager::removeBackend(const QString &name) {
  for (int i = 0; i < m_backendList.size(); ++i) {
    if (m_backendList[i].toMap().value("name").toString() == name) {
      m_backendList.removeAt(i);
      emit backendListChanged();
      saveConfig();
      return;
    }
  }
}

QString ConfigManager::projectsRoot() const {
  QString dirPath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dirPath);
  return dirPath + "/Projects";
}

bool ConfigManager::createProject(const QString &projectName) {
  if (projectName.trimmed().isEmpty())
    return false;

  QString projectDir = projectsRoot() + "/" + projectName.trimmed();

  // Check if project already exists
  if (QDir(projectDir).exists()) {
    qWarning() << "Project already exists:" << projectDir;
    return false;
  }

  QDir rootDir;
  rootDir.mkpath(projectDir);
  rootDir.mkpath(projectDir + "/IMG");
  rootDir.mkpath(projectDir + "/PDF");

  // Create project database file
  QString dbPath = projectDir + "/" + projectName.trimmed() + ".db";
  QFile dbFile(dbPath);
  if (!dbFile.open(QIODevice::WriteOnly)) {
    qWarning() << "Failed to create database file:" << dbPath;
    return false;
  }
  dbFile.close();

  setLastProject(projectName.trimmed());
  saveConfig();
  refreshProjectList();

  emit projectCreated(projectName.trimmed());
  qDebug() << "Project created:" << projectDir << "(DB:" << dbPath << ")";
  return true;
}

bool ConfigManager::switchToProject(const QString &projectName) {
  if (projectName.trimmed().isEmpty())
    return false;

  QString projectDir = projectsRoot() + "/" + projectName.trimmed();
  if (!QDir(projectDir).exists()) {
    qWarning() << "Project does not exist:" << projectDir;
    return false;
  }

  setLastProject(projectName.trimmed());
  return true;
}

bool ConfigManager::deleteProjects(const QStringList &projectNames) {
  if (projectNames.isEmpty())
    return false;

  // Check if active project is among those to delete
  bool activeWillBeDeleted = false;
  for (const QString &name : projectNames) {
    if (name.trimmed() == m_lastProject.trimmed()) {
      activeWillBeDeleted = true;
      break;
    }
  }

  for (const QString &projectName : projectNames) {
    if (projectName.trimmed().isEmpty())
      continue;

    QString projectDir = projectsRoot() + "/" + projectName.trimmed();
    QDir dir(projectDir);
    if (!dir.exists()) {
      qWarning() << "Project does not exist, skipping:" << projectDir;
      continue;
    }

    // Remove all contents and the project folder itself
    dir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    const QFileInfoList entries = dir.entryInfoList();
    for (const QFileInfo &entry : entries) {
      if (entry.isDir()) {
        QDir(entry.absoluteFilePath()).removeRecursively();
      } else {
        QFile::remove(entry.absoluteFilePath());
      }
    }
    dir.rmdir(projectDir);

    emit projectDeleted(projectName.trimmed());
    qDebug() << "Project deleted:" << projectDir;
  }

  refreshProjectList();

  // If the active project was deleted, switch to another
  if (activeWillBeDeleted) {
    if (!m_projectList.isEmpty()) {
      switchToProject(m_projectList.first());
    } else {
      createProject("Default");
    }
  }

  return true;
}

bool ConfigManager::resetActiveProject() {
  QString name = m_lastProject.trimmed();
  if (name.isEmpty()) {
    qWarning() << "No active project to reset";
    return false;
  }

  QString projectDir = projectsRoot() + "/" + name;
  QDir dir(projectDir);
  if (!dir.exists()) {
    qWarning() << "Active project directory does not exist:" << projectDir;
    return false;
  }

  // Remove all contents
  dir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
  const QFileInfoList entries = dir.entryInfoList();
  for (const QFileInfo &entry : entries) {
    if (entry.isDir()) {
      QDir(entry.absoluteFilePath()).removeRecursively();
    } else {
      QFile::remove(entry.absoluteFilePath());
    }
  }

  // Recreate subdirectories
  dir.mkpath(projectDir + "/IMG");
  dir.mkpath(projectDir + "/PDF");

  // Recreate empty database
  QString dbPath = projectDir + "/" + name + ".db";
  QFile dbFile(dbPath);
  if (!dbFile.open(QIODevice::WriteOnly)) {
    qWarning() << "Failed to recreate database file:" << dbPath;
    return false;
  }
  dbFile.close();

  refreshProjectList();
  qDebug() << "Project reset:" << projectDir;
  return true;
}

void ConfigManager::ensureModelsDirectory() {
  QDir modelsDir(modelsRoot());
  if (!modelsDir.exists()) {
    modelsDir.mkpath("Embedders");
    modelsDir.mkpath("Summarizers");
  }
}

QString ConfigManager::modelsRoot() const {
  QString dirPath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  return dirPath + "/Models";
}

QString ConfigManager::modelsPath() const { return modelsRoot(); }

QString ConfigManager::embeddersPath() const {
  return modelsRoot() + "/Embedders";
}

QString ConfigManager::summarizersPath() const {
  return modelsRoot() + "/Summarizers";
}

void ConfigManager::addLocalSummarizer(const QString &sourcePath) {
  // Resolve file:// URI if needed
  QString srcPath = sourcePath;
  if (srcPath.startsWith("file://")) {
    srcPath = QUrl(srcPath).toLocalFile();
  }

  QFile src(srcPath);
  if (!src.exists()) {
    qWarning() << "[ConfigManager] Source file does not exist:" << srcPath;
    emit modelCopyFinished(false, QString(),
                           "Source file does not exist: " + srcPath);
    return;
  }

  // Create a subfolder named after the model (without extension).
  QString modelName = QFileInfo(srcPath).completeBaseName();
  QString modelFolder = summarizersPath() + "/" + modelName;
  QDir().mkpath(modelFolder);

  QMetaObject::invokeMethod(
      m_copyWorker,
      [this, srcPath, modelFolder]() {
        m_copyWorker->run(srcPath, modelFolder);
      },
      Qt::QueuedConnection);
}

void ConfigManager::refreshProjectList() {
  QDir projectsDir(projectsRoot());
  if (!projectsDir.exists()) {
    m_projectList.clear();
    return;
  }
  projectsDir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
  m_projectList = projectsDir.entryList();
  emit projectListChanged();
}

QString ConfigManager::projectPath(const QString &projectName) const {
  return projectsRoot() + "/" + projectName;
}

QString ConfigManager::projectDbPath(const QString &projectName) const {
  return projectsRoot() + "/" + projectName + "/" + projectName + ".db";
}

QString ConfigManager::projectImgPath(const QString &projectName) const {
  return projectsRoot() + "/" + projectName + "/" + "IMG";
}
QString ConfigManager::projectPdfPath(const QString &projectName) const {
  return projectsRoot() + "/" + projectName + "/" + "PDF";
}

void ConfigManager::deleteImages(const QStringList &hashList,
                                 const QString &imgPath) const {
  QDir imgDir(imgPath);
  if (!imgDir.exists()) {
    qWarning() << "Image directory does not exist:" << imgPath;
    return;
  }

  for (const QString &hash : hashList) {
    // Match files like: 4ad90c343ca0d5a05e67f80da33a724b_page_001.png
    imgDir.setNameFilters({hash + "_page_*.png"});
    const QFileInfoList matchingFiles = imgDir.entryInfoList();
    for (const QFileInfo &fileInfo : matchingFiles) {
      if (QFile::remove(fileInfo.absoluteFilePath())) {
        qDebug() << "Deleted image:" << fileInfo.absoluteFilePath();
      } else {
        qWarning() << "Failed to delete image:" << fileInfo.absoluteFilePath();
      }
    }
    if (matchingFiles.isEmpty()) {
      qDebug() << "No images found for hash:" << hash;
    }
  }
}

// ── ModelCopyWorker ──────────────────────────────────────────────────

ModelCopyWorker::ModelCopyWorker(QObject *parent) : QObject(parent) {}

void ModelCopyWorker::run(const QString &sourcePath, const QString &destDir) {
  QString srcPath = sourcePath;
  if (srcPath.startsWith("file://")) {
    srcPath = QUrl(srcPath).toLocalFile();
  }

  QFile src(srcPath);
  if (!src.exists()) {
    emit finished(false, QString(), "Source file not found: " + srcPath);
    return;
  }

  QString fileName = QFileInfo(src).fileName();
  QString destPath = destDir + "/" + fileName;

  QDir d;
  d.mkpath(destDir);

  // Remove existing destination if present
  if (QFile::exists(destPath) && !QFile::remove(destPath)) {
    emit finished(false, QString(), "Cannot remove existing file: " + destPath);
    return;
  }

  if (!src.open(QIODevice::ReadOnly)) {
    emit finished(false, QString(), "Cannot open source file: " + srcPath);
    return;
  }

  qint64 total = src.size();
  if (total <= 0) {
    emit finished(false, QString(),
                  "Cannot determine source file size: " + srcPath);
    return;
  }

  QFile dest(destPath);
  if (!dest.open(QIODevice::WriteOnly)) {
    emit finished(false, QString(),
                  "Cannot open destination for writing: " + destPath);
    return;
  }

  const qint64 bufSize = 1024 * 1024; // 1 MiB chunks
  char buf[bufSize];
  qint64 copied = 0;

  while (!src.atEnd()) {
    qint64 bytesRead = src.read(buf, bufSize);
    if (bytesRead <= 0)
      break;
    copied += dest.write(buf, bytesRead);
    int pct = static_cast<int>(copied * 100 / total);
    emit progress(pct, fileName);
  }
  dest.close();
  src.close();

  if (copied != total) {
    QFile::remove(destPath);
    emit finished(false, QString(),
                  "Copy incomplete: " + QString::number(copied) + "/" +
                      QString::number(total) + " bytes");
    return;
  }

  emit progress(100, fileName);
  emit finished(true, destPath, QString());
}
