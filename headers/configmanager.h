#pragma once
#include <QObject>
#include <QString>

class ConfigManager : public QObject {
    Q_OBJECT
    
    // Expose your configuration fields directly to QML
    Q_PROPERTY(int activeThemeIndex READ activeThemeIndex WRITE setActiveThemeIndex NOTIFY activeThemeIndexChanged)
    Q_PROPERTY(QString lastProjectDatabasePath READ lastProjectDatabasePath WRITE setLastProjectDatabasePath NOTIFY lastProjectDatabasePathChanged)
    Q_PROPERTY(QString lastOpenedPdf READ lastOpenedPdf WRITE setLastOpenedPdf NOTIFY lastOpenedPdfChanged)

public:
    explicit ConfigManager(QObject *parent = nullptr);

    // Getters
    int activeThemeIndex() const { return m_activeThemeIndex; }
    QString lastProjectDatabasePath() const { return m_lastProjectDatabasePath; }
    QString lastOpenedPdf() const { return m_lastOpenedPdf; }

    // Setters (Will trigger auto-saving to disk)
    void setActiveThemeIndex(int index);
    void setLastProjectDatabasePath(const QString &path);
    void setLastOpenedPdf(const QString &path);

    // File Operations
    Q_INVOKABLE void loadConfig();
    Q_INVOKABLE void saveConfig();

signals:
    void activeThemeIndexChanged();
    void lastProjectDatabasePathChanged();
    void lastOpenedPdfChanged();

private:
    QString getFilePath() const; // Helper to get operating system's safe save path

    int m_activeThemeIndex = 0;
    QString m_lastProjectDatabasePath = "";
    QString m_lastOpenedPdf = "";
};