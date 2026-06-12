#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <qtmetamacros.h>

class ConfigManager : public QObject {
  Q_OBJECT

  // Expose your configuration fields directly to QML
  Q_PROPERTY(unsigned short activeThemeIndex READ activeThemeIndex WRITE
                 setActiveThemeIndex NOTIFY activeThemeIndexChanged)
  Q_PROPERTY(QString lastProject READ lastProject WRITE
                 setLastProject NOTIFY lastProjectChanged)
  Q_PROPERTY(QString activeBackend READ activeBackend WRITE setActiveBackend
                 NOTIFY activeBackendChanged)
  Q_PROPERTY(unsigned int contextWindow READ contextWindow WRITE setContextWindow NOTIFY
                 contextWindowChanged)
  Q_PROPERTY(double modelTemperature READ modelTemperature WRITE setModelTemperature
                 NOTIFY modelTemperatureChanged)
  Q_PROPERTY(QStringList backendList READ backendList WRITE setBackendList NOTIFY backendListChanged)

public:
  explicit ConfigManager(QObject *parent = nullptr);

  // Getters
  unsigned short activeThemeIndex() const { return m_activeThemeIndex; }
  QString lastProject() const { return m_lastProject; }
  QString activeBackend() const { return m_activeBackend; }
  unsigned int contextWindow() const { return m_contextWindow; }
  float modelTemperature() const { return m_modelTemperature; }
  QStringList backendList() const { return m_backendList; }

  // Setters (Will trigger auto-saving to disk)
  void setActiveThemeIndex(int index);
  void setLastProject(const QString &path);
  void setActiveBackend(const QString &backend);
  void setContextWindow(unsigned int window);
  void setModelTemperature(double temperature);
  void setBackendList(const QStringList &backends);

  // File Operations
  Q_INVOKABLE void loadConfig();
  Q_INVOKABLE void saveConfig();

signals:
  void activeThemeIndexChanged();
  void lastProjectChanged();
  void activeBackendChanged();
  void contextWindowChanged();
  void modelTemperatureChanged();
  void backendListChanged();

private:
  QString
  getFilePath() const; // Helper to get operating system's safe save path

  int m_activeThemeIndex = 0;
  QString m_lastProject = "";
  QString m_activeBackend = "";
  unsigned int m_contextWindow = 4000;
  double m_modelTemperature = 0.1;
  QStringList m_backendList;
};