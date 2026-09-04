#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class ThemeManager : public QObject {
    Q_OBJECT
    // Expose the list of themes to QML
    Q_PROPERTY(QVariantList themes READ themes NOTIFY themesChanged)
    // Add a global property for the active theme
    Q_PROPERTY(QVariantMap activeTheme READ activeTheme NOTIFY activeThemeChanged)

public:
    explicit ThemeManager(QObject *parent = nullptr);
    QVariantList themes() const { return m_themes; }
    QVariantMap activeTheme() const { return m_activeTheme; }

    // Call this on startup to load files from a folder
    Q_INVOKABLE void loadThemesFromFolder(const QString &folderPath);
    // Add a slot/method to update the theme from QML
    Q_INVOKABLE void setThemeIndex(int index);

signals:
    void themesChanged();
    void activeThemeChanged();

private:
    QVariantList m_themes;
    QVariantMap m_activeTheme;
    int m_currentIndex = 0;
};
