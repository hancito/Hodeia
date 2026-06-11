#include "headers/configmanager.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

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
        m_lastProjectDatabasePath = obj.value("lastProjectDatabasePath").toString("");
        m_lastOpenedPdf = obj.value("lastOpenedPdf").toString("");
    }
}

void ConfigManager::saveConfig() {
    QFile file(getFilePath());
    if (!file.open(QIODevice::WriteOnly)) return;

    QJsonObject obj;
    obj["activeThemeIndex"] = m_activeThemeIndex;
    obj["lastProjectDatabasePath"] = m_lastProjectDatabasePath;
    obj["lastOpenedPdf"] = m_lastOpenedPdf;

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

void ConfigManager::setLastProjectDatabasePath(const QString &path) {
    if (m_lastProjectDatabasePath != path) {
        m_lastProjectDatabasePath = path;
        emit lastProjectDatabasePathChanged();
        saveConfig();
    }
}

void ConfigManager::setLastOpenedPdf(const QString &path) {
    if (m_lastOpenedPdf != path) {
        m_lastOpenedPdf = path;
        emit lastOpenedPdfChanged();
        saveConfig();
    }
}