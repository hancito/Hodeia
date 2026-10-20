#include "configmanager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUrl>
#include <functional>

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
    (void)createProject("Default");
  }
  ensureModelsDirectory();
  seedBundledModels();
  selectDefaultModel();
  refreshProjectList();
}

ConfigManager::~ConfigManager() { saveConfig(); }

QString ConfigManager::getFilePath() const {
  // Finds location like: AppData/Roaming/Hodeia/ or ~/.local/share/Hodeia/
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
    m_openAlexApiKey = obj.value("openAlexApiKey").toString("");
    m_language = obj.value("language").toString("en");
    // Support string "visualMode", old int "visualMode", and legacy
    // "useVisualContext" bool.
    if (obj.contains("visualMode")) {
      QJsonValue v = obj.value("visualMode");
      if (v.isString()) {
        // Map legacy English string keys to the enum
        const QString s = v.toString();
        if (s == "Text only")
          m_visualModeIndex = 0;
        else if (s == "Full (all pages)")
          m_visualModeIndex = 2;
        else
          m_visualModeIndex = 1;
      } else {
        m_visualModeIndex = v.toInt(1);
      }
    } else if (obj.contains("useVisualContext")) {
      m_visualModeIndex = obj.value("useVisualContext").toBool(true) ? 1 : 0;
    }

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
  obj["openAlexApiKey"] = m_openAlexApiKey;
  obj["visualMode"] = m_visualModeIndex;
  obj["language"] = m_language;

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

void ConfigManager::setOpenAlexApiKey(const QString &key) {
  if (m_openAlexApiKey != key) {
    m_openAlexApiKey = key;
    emit openAlexApiKeyChanged();
    saveConfig();
  }
}

QString ConfigManager::visualMode() const {
  switch (m_visualModeIndex) {
  case 0:
    return tr("Text only");
  case 2:
    return tr("Full (all pages)");
  default:
    return tr("Reduced (figures/tables)");
  }
}

QStringList ConfigManager::visualModeList() const {
  return {tr("Text only"), tr("Reduced (figures/tables)"),
          tr("Full (all pages)")};
}

void ConfigManager::setVisualMode(const QString &mode) {
  int idx;
  if (mode == tr("Text only"))
    idx = 0;
  else if (mode == tr("Full (all pages)"))
    idx = 2;
  else
    idx = 1;
  if (m_visualModeIndex != idx) {
    m_visualModeIndex = idx;
    emit visualModeChanged();
    saveConfig();
  }
}

QString ConfigManager::language() const { return m_language; }

void ConfigManager::setLanguage(const QString &lang) {
  if (m_language != lang) {
    m_language = lang;
    emit languageChanged();
    saveConfig();
  }
}

VisualMode ConfigManager::visualModeEnum() const {
  switch (m_visualModeIndex) {
  case 0:
    return VisualMode::TextOnly;
  case 2:
    return VisualMode::Full;
  default:
    return VisualMode::Reduced;
  }
}

bool ConfigManager::useVisualContext() const {
  return visualModeEnum() != VisualMode::TextOnly;
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

std::expected<void, QString>
ConfigManager::createProject(const QString &projectName) {
  if (projectName.trimmed().isEmpty())
    return std::unexpected{tr("Project name is empty")};

  QString projectDir = projectsRoot() + "/" + projectName.trimmed();

  // Check if project already exists
  if (QDir(projectDir).exists()) {
    qWarning() << "Project already exists:" << projectDir;
    return std::unexpected{
        tr("Project already exists: %1").arg(projectName.trimmed())};
  }

  QDir rootDir;
  rootDir.mkpath(projectDir);
  rootDir.mkpath(projectDir + "/IMG");

  // Create project database file
  QString dbPath = projectDir + "/" + projectName.trimmed() + ".db";
  QFile dbFile(dbPath);
  if (!dbFile.open(QIODevice::WriteOnly)) {
    qWarning() << "Failed to create database file:" << dbPath;
    return std::unexpected{
        tr("Failed to create database file: %1").arg(dbPath)};
  }
  dbFile.close();

  setLastProject(projectName.trimmed());
  saveConfig();
  refreshProjectList();

  emit projectCreated(projectName.trimmed());
  qDebug() << "Project created:" << projectDir << "(DB:" << dbPath << ")";
  return {};
}

std::expected<void, QString>
ConfigManager::switchToProject(const QString &projectName) {
  if (projectName.trimmed().isEmpty())
    return std::unexpected{tr("Project name is empty")};

  QString projectDir = projectsRoot() + "/" + projectName.trimmed();
  if (!QDir(projectDir).exists()) {
    qWarning() << "Project does not exist:" << projectDir;
    return std::unexpected{
        tr("Project does not exist: %1").arg(projectName.trimmed())};
  }

  setLastProject(projectName.trimmed());
  return {};
}

std::expected<void, QString>
ConfigManager::deleteProjects(const QStringList &projectNames) {
  if (projectNames.isEmpty())
    return std::unexpected{tr("No project names provided")};

  // Check if active project is among those to delete
  bool activeWillBeDeleted = false;
  for (const QString &name : projectNames) {
    if (name.trimmed() == m_lastProject.trimmed()) {
      activeWillBeDeleted = true;
      break;
    }
  }

  bool anyDeleted = false;

  for (const QString &projectName : projectNames) {
    if (projectName.trimmed().isEmpty())
      continue;

    QString projectDir = projectsRoot() + "/" + projectName.trimmed();
    QDir dir(projectDir);
    if (!dir.exists()) {
      qWarning() << "Project does not exist, skipping:" << projectDir;
      continue;
    }

    anyDeleted = true;

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
      (void)switchToProject(m_projectList.first());
    } else {
      (void)createProject("Default");
    }
  }

  if (!anyDeleted)
    return std::unexpected{tr("No projects were deleted (none existed)")};

  return {};
}

std::expected<void, QString> ConfigManager::resetActiveProject() {
  QString name = m_lastProject.trimmed();
  if (name.isEmpty()) {
    qWarning() << "No active project to reset";
    return std::unexpected{tr("No active project to reset")};
  }

  QString projectDir = projectsRoot() + "/" + name;
  QDir dir(projectDir);
  if (!dir.exists()) {
    qWarning() << "Active project directory does not exist:" << projectDir;
    return std::unexpected{
        tr("Active project directory does not exist: %1").arg(projectDir)};
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

  // Recreate empty database
  QString dbPath = projectDir + "/" + name + ".db";
  QFile dbFile(dbPath);
  if (!dbFile.open(QIODevice::WriteOnly)) {
    qWarning() << "Failed to recreate database file:" << dbPath;
    return std::unexpected{
        tr("Failed to recreate database file: %1").arg(dbPath)};
  }
  dbFile.close();

  refreshProjectList();
  qDebug() << "Project reset:" << projectDir;
  return {};
}

void ConfigManager::ensureModelsDirectory() {
  QDir modelsDir(modelsRoot());
  if (!modelsDir.exists()) {
    modelsDir.mkpath("Embedders");
    modelsDir.mkpath("Summarizers");
  }
}

void ConfigManager::seedBundledModels() {
  // The bundled model tree (Embedders/ + Summarizers/) ships in
  // Resources/Models/ next to the executable in a normal install.  At startup
  // we copy every file that is missing from the data dir into place.  This is
  // idempotent: a fresh install seeds the whole tree, while a version bump only
  // fills in newly-bundled models and never overwrites files the user already
  // has (e.g. a model they deleted is not re-seeded, a renamed file is added).
  const QString appDir = QCoreApplication::applicationDirPath();
  const QString srcRoot = appDir + "/Resources/Models";
  if (!QDir(srcRoot).exists()) {
    qWarning() << "[Config] No bundled model tree found at" << srcRoot;
    return;
  }

  const QString destRoot = modelsRoot();

  // Recursively copy files from `src` into `dest`, creating directories as
  // needed and skipping files that already exist at the destination.
  std::function<void(const QString &, const QString &)> copyMissing;
  copyMissing = [&](const QString &src, const QString &dest) {
    QDir srcDir(src);
    if (!srcDir.exists())
      return;
    srcDir.setFilter(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : srcDir.entryInfoList()) {
      const QString destEntry = dest + "/" + entry.fileName();
      if (entry.isDir()) {
        copyMissing(entry.absoluteFilePath(), destEntry);
      } else if (!QFile::exists(destEntry)) {
        QDir().mkpath(QFileInfo(destEntry).absolutePath());
        if (QFile::copy(entry.absoluteFilePath(), destEntry)) {
          qInfo() << "[Config] Seeded bundled model:" << destEntry;
        } else {
          qWarning() << "[Config] Failed to copy bundled model from"
                     << entry.absoluteFilePath() << "to" << destEntry;
        }
      }
    }
  };

  copyMissing(srcRoot, destRoot);
}

void ConfigManager::selectDefaultModel() {
  // If the user has already picked a model, leave it alone.
  if (!m_activeModel.isEmpty())
    return;

  // Pick the first local model that actually has a usable (non-mmproj) GGUF,
  // so a fresh install works out of the box without the user touching Settings.
  QDir dir(summarizersPath());
  if (!dir.exists())
    return;
  const QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString &name : subdirs) {
    const QStringList ggufs =
        QDir(dir.absoluteFilePath(name))
            .entryList(QStringList{"*.gguf"}, QDir::Files | QDir::NoSymLinks);
    for (const QString &f : ggufs) {
      if (!f.contains("mmproj", Qt::CaseInsensitive)) {
        setActiveModel(name);
        qDebug() << "[Config] Auto-selected default model:" << name;
        return;
      }
    }
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
