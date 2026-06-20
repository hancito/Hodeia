#include "headers/configmanager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

ConfigManager::ConfigManager(QObject *parent) : QObject(parent) {
  loadConfig(); // Automatically load the file when instantiated

  // Create "Default" project if no projects exist yet
  QDir projRoot(projectsRoot());
  if (!projRoot.exists() ||
      projRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
    createProject("Default");
  }
  refreshProjectList();
}

ConfigManager::~ConfigManager() { saveConfig(); }

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
    m_contextWindow =
        static_cast<unsigned int>(obj.value("contextWindow").toInt(4000));
    m_modelTemperature = obj.value("modelTemperature").toDouble(0.1);

    // Load backend list from JSON array
    QJsonArray backendArray = obj.value("backendList").toArray();
    m_backendList.clear();
    for (const auto &item : backendArray) {
      m_backendList << item.toString();
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
  obj["contextWindow"] = static_cast<qint64>(m_contextWindow);
  obj["modelTemperature"] = m_modelTemperature;

  // Save backend list as JSON array
  QJsonArray backendArray;
  for (const auto &backend : m_backendList) {
    backendArray.append(backend);
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

void ConfigManager::setBackendList(const QStringList &backends) {
  if (m_backendList != backends) {
    m_backendList = backends;
    emit backendListChanged();
    saveConfig();
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
