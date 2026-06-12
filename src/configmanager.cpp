#include "headers/configmanager.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ConfigManager::ConfigManager(QObject *parent) : QObject(parent) {
    loadConfig(); // Automatically load the file when instantiated
}

QString ConfigManager::getFilePath() const {
    // Finds location like: AppData/Roaming/Paperize/ or ~/.local/share/Paperize/
    QString dirPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dirPath); // Creates directory if it doesn't exist yet
    return dirPath + "/config.json";
}

void ConfigManager::loadConfig() {
    QFile file(getFilePath());
    if (!file.open(QIODevice::ReadOnly)) return; // Use hardcoded defaults if file missing

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        m_activeThemeIndex = obj.value("activeThemeIndex").toInt(0);
        m_lastProject = obj.value("lastProject").toString("");
        m_activeBackend = obj.value("activeBackend").toString("");
        m_contextWindow = static_cast<unsigned int>(obj.value("contextWindow").toInt(4000));
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
    if (!file.open(QIODevice::WriteOnly)) return;

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
