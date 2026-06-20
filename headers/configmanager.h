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
  Q_PROPERTY(QString lastProject READ lastProject WRITE setLastProject NOTIFY
                 lastProjectChanged)
  Q_PROPERTY(QString activeBackend READ activeBackend WRITE setActiveBackend
                 NOTIFY activeBackendChanged)
  Q_PROPERTY(unsigned int contextWindow READ contextWindow WRITE
                 setContextWindow NOTIFY contextWindowChanged)
  Q_PROPERTY(double modelTemperature READ modelTemperature WRITE
                 setModelTemperature NOTIFY modelTemperatureChanged)
  Q_PROPERTY(QStringList backendList READ backendList WRITE setBackendList
                 NOTIFY backendListChanged)
  Q_PROPERTY(QStringList projectList READ projectList NOTIFY projectListChanged)

public:
  explicit ConfigManager(QObject *parent = nullptr);
  ~ConfigManager() override;

  // Getters
  unsigned short activeThemeIndex() const { return m_activeThemeIndex; }
  QString lastProject() const { return m_lastProject; }
  QString activeBackend() const { return m_activeBackend; }
  unsigned int contextWindow() const { return m_contextWindow; }
  float modelTemperature() const { return m_modelTemperature; }
  QStringList backendList() const { return m_backendList; }
  QStringList projectList() const { return m_projectList; }

  // Setters (Will trigger auto-saving to disk)
  void setActiveThemeIndex(int index);
  void setLastProject(const QString &path);
  void setActiveBackend(const QString &backend);
  void setContextWindow(unsigned int window);
  void setModelTemperature(double temperature);
  void setBackendList(const QStringList &backends);

  // Project Operations
  Q_INVOKABLE bool createProject(const QString &projectName);
  Q_INVOKABLE bool switchToProject(const QString &projectName);
  Q_INVOKABLE bool deleteProjects(const QStringList &projectNames);
  Q_INVOKABLE bool resetActiveProject();
  Q_INVOKABLE QString projectPath(const QString &projectName) const;
  Q_INVOKABLE QString projectDbPath(const QString &projectName) const;
  Q_INVOKABLE QString projectImgPath(const QString &projectName) const;
  Q_INVOKABLE QString projectPdfPath(const QString &projectName) const;
  Q_INVOKABLE void deleteImages(const QStringList &hashList,
                                const QString &imgPath) const;

  // File Operations
  Q_INVOKABLE void loadConfig();
  Q_INVOKABLE void saveConfig();

signals:
  void projectCreated(const QString &projectName);
  void projectDeleted(const QString &projectName);
  void projectListChanged();
  void activeThemeIndexChanged();
  void lastProjectChanged();
  void activeBackendChanged();
  void contextWindowChanged();
  void modelTemperatureChanged();
  void backendListChanged();

private:
  void refreshProjectList();
  QString getFilePath() const;
  QString projectsRoot() const;

  int m_activeThemeIndex = 0;
  QString m_lastProject = "";
  QString m_activeBackend = "";
  unsigned int m_contextWindow = 4000;
  double m_modelTemperature = 0.1;
  QStringList m_backendList;
  QStringList m_projectList;
};
