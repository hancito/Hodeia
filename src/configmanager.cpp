#include "headers/configmanager.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUrl>

ConfigManager::ConfigManager(QObject *parent) : QObject(parent) {
  // Probe hardware only when hardware.json does not yet exist.
  // On subsequent launches the cached file is loaded directly.
  if (!QFile::exists(hardwareJsonPath())) {
    detectAndSaveHardware();
  } else {
    QFile hwFile(hardwareJsonPath());
    if (hwFile.open(QIODevice::ReadOnly)) {
      QJsonDocument doc = QJsonDocument::fromJson(hwFile.readAll());
      if (doc.isObject()) {
        m_hardwareInfo = hardwareFromVariantMap(doc.object().toVariantMap());
      }
    }
  }

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
    m_activeModel = obj.value("activeModel").toString("");
    m_contextWindow =
        static_cast<unsigned int>(obj.value("contextWindow").toInt(4000));
    m_modelTemperature = obj.value("modelTemperature").toDouble(0.1);
    m_useVisualContext = obj.value("useVisualContext").toBool(true);

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
  obj["useVisualContext"] = m_useVisualContext;

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

std::expected<void, QString>
ConfigManager::createProject(const QString &projectName) {
  if (projectName.trimmed().isEmpty())
    return std::unexpected{QStringLiteral("Project name is empty")};

  QString projectDir = projectsRoot() + "/" + projectName.trimmed();

  // Check if project already exists
  if (QDir(projectDir).exists()) {
    qWarning() << "Project already exists:" << projectDir;
    return std::unexpected{QStringLiteral("Project already exists: ") +
                           projectName.trimmed()};
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
    return std::unexpected{QStringLiteral("Failed to create database file: ") +
                           dbPath};
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
    return std::unexpected{QStringLiteral("Project name is empty")};

  QString projectDir = projectsRoot() + "/" + projectName.trimmed();
  if (!QDir(projectDir).exists()) {
    qWarning() << "Project does not exist:" << projectDir;
    return std::unexpected{QStringLiteral("Project does not exist: ") +
                           projectName.trimmed()};
  }

  setLastProject(projectName.trimmed());
  return {};
}

std::expected<void, QString>
ConfigManager::deleteProjects(const QStringList &projectNames) {
  if (projectNames.isEmpty())
    return std::unexpected{QStringLiteral("No project names provided")};

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
    return std::unexpected{
        QStringLiteral("No projects were deleted (none existed)")};

  return {};
}

std::expected<void, QString> ConfigManager::resetActiveProject() {
  QString name = m_lastProject.trimmed();
  if (name.isEmpty()) {
    qWarning() << "No active project to reset";
    return std::unexpected{QStringLiteral("No active project to reset")};
  }

  QString projectDir = projectsRoot() + "/" + name;
  QDir dir(projectDir);
  if (!dir.exists()) {
    qWarning() << "Active project directory does not exist:" << projectDir;
    return std::unexpected{
        QStringLiteral("Active project directory does not exist: ") +
        projectDir};
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
    return std::unexpected{
        QStringLiteral("Failed to recreate database file: ") + dbPath};
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

// ── Hardware detection ──────────────────────────────────────────────

void ConfigManager::detectAndSaveHardware() {
  m_hardwareInfo = detectHardware();

  QFile file(hardwareJsonPath());
  if (!file.open(QIODevice::WriteOnly)) {
    qWarning() << "[ConfigManager] Could not write" << hardwareJsonPath();
    return;
  }
  QVariantMap v = hardwareToVariantMap(m_hardwareInfo);
  QJsonObject obj;
  for (auto it = v.constBegin(); it != v.constEnd(); ++it) {
    if (it.key() == "gpus") {
      QJsonArray arr;
      for (const auto &gpuVar : it.value().toList()) {
        QJsonObject gpuObj;
        QVariantMap m = gpuVar.toMap();
        gpuObj["name"] = m["name"].toString();
        gpuObj["vram_bytes"] = m["vram_bytes"].toDouble();
        gpuObj["vendor"] = m["vendor"].toString();
        gpuObj["has_vulkan"] = m["has_vulkan"].toBool();
        gpuObj["has_metal"] = m["has_metal"].toBool();
        gpuObj["has_cuda"] = m["has_cuda"].toBool();
        arr.append(gpuObj);
      }
      obj["gpus"] = arr;
    } else if (it.key() == "numa_nodes") {
      QJsonArray arr;
      for (const auto &nodeVar : it.value().toList()) {
        QJsonObject nodeObj;
        QVariantMap m = nodeVar.toMap();
        nodeObj["node_id"] = m["node_id"].toInt();
        nodeObj["memory_bytes"] = m["memory_bytes"].toDouble();
        QJsonArray cpuArr;
        for (const auto &cpu : m["cpus"].toList())
          cpuArr.append(cpu.toInt());
        nodeObj["cpus"] = cpuArr;
        arr.append(nodeObj);
      }
      obj["numa_nodes"] = arr;
    } else {
      obj[it.key()] = QJsonValue::fromVariant(it.value());
    }
  }
  QJsonDocument doc(obj);
  file.write(doc.toJson());
  emit hardwareInfoChanged();
  qDebug() << "[ConfigManager] Hardware detected and saved to"
           << hardwareJsonPath();
}

QVariantMap
ConfigManager::hardwareToVariantMap(const HardwareInfo &info) const {
  QVariantMap v;
  v["cpu_threads"] = info.cpu_threads;
  v["cpu_cores"] = info.cpu_cores;
  v["cpu_model"] = info.cpu_model;
  v["total_ram_bytes"] = static_cast<double>(info.total_ram_bytes);
  QVariantList gpuList;
  for (const auto &gpu : info.gpus) {
    QVariantMap g;
    g["name"] = gpu.name;
    g["vram_bytes"] = static_cast<double>(gpu.vram_bytes);
    g["vendor"] = gpu.vendor;
    g["has_vulkan"] = gpu.has_vulkan;
    g["has_metal"] = gpu.has_metal;
    g["has_cuda"] = gpu.has_cuda;
    gpuList.append(g);
  }
  v["gpus"] = gpuList;
  v["has_npu"] = info.has_npu;

  QVariantList numaList;
  for (const auto &node : info.numa_nodes) {
    QVariantMap n;
    n["node_id"] = node.node_id;
    n["memory_bytes"] = static_cast<double>(node.memory_bytes);
    QVariantList cpuList;
    for (int c : node.cpus)
      cpuList.append(c);
    n["cpus"] = cpuList;
    numaList.append(n);
  }
  v["numa_nodes"] = numaList;

  v["detection_time"] = info.detection_time;
  return v;
}

HardwareInfo ConfigManager::hardwareFromVariantMap(const QVariantMap &v) const {
  HardwareInfo info;
  info.cpu_threads = v["cpu_threads"].toInt();
  info.cpu_cores = v["cpu_cores"].toInt();
  info.cpu_model = v["cpu_model"].toString();
  info.total_ram_bytes = static_cast<qint64>(v["total_ram_bytes"].toDouble());
  info.has_npu = v["has_npu"].toBool();
  info.detection_time = v["detection_time"].toString();
  for (const auto &entry : v["gpus"].toList()) {
    QVariantMap g = entry.toMap();
    GpuInfo gpu;
    gpu.name = g["name"].toString();
    gpu.vram_bytes = static_cast<qint64>(g["vram_bytes"].toDouble());
    gpu.vendor = g["vendor"].toString();
    gpu.has_vulkan = g["has_vulkan"].toBool();
    gpu.has_metal = g["has_metal"].toBool();
    gpu.has_cuda = g["has_cuda"].toBool();
    info.gpus.append(gpu);
  }
  for (const auto &entry : v["numa_nodes"].toList()) {
    QVariantMap n = entry.toMap();
    NumaNodeInfo node;
    node.node_id = n["node_id"].toInt();
    node.memory_bytes = static_cast<qint64>(n["memory_bytes"].toDouble());
    for (const auto &cpu : n["cpus"].toList())
      node.cpus.append(cpu.toInt());
    info.numa_nodes.append(node);
  }
  return info;
}

QVariantMap ConfigManager::hardwareInfo() const {
  return hardwareToVariantMap(m_hardwareInfo);
}

QString ConfigManager::hardwareJsonPath() const {
  QString dirPath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dirPath);
  return dirPath + "/hardware.json";
}
