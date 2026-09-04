#include "thememanager.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

ThemeManager::ThemeManager(QObject *parent) : QObject(parent) {}

void ThemeManager::loadThemesFromFolder(const QString &folderPath) {
    m_themes.clear();
    QDir dir(folderPath);
    
    // Look for all .json files in the folder
    QStringList filters;
    filters << "*.json";
    QStringList files = dir.entryList(filters, QDir::Files);

    for (const QString &fileName : files) {
        QFile file(dir.filePath(fileName));
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isObject()) {
                // Convert JSON object directly to a QVariantMap (JS Object in QML)
                m_themes.append(doc.object().toVariantMap());
            }
        }
    }
    if (!m_themes.isEmpty()) {
        setThemeIndex(0); 
    }
    emit themesChanged();
}


void ThemeManager::setThemeIndex(int index) {
    if (index >= 0 && index < m_themes.size() && m_currentIndex != index) {
        m_currentIndex = index;
        m_activeTheme = m_themes.at(index).toMap();
        emit activeThemeChanged(); // Notifies ALL QML windows instantly
    }
}