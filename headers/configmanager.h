#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <qtmetamacros.h>

#include "headers/hardwaredetector.h"

class QThread;

/// Background worker for copying model files with progress reporting.
class ModelCopyWorker : public QObject {
  Q_OBJECT
public:
  explicit ModelCopyWorker(QObject *parent = nullptr);
  Q_INVOKABLE void run(const QString &sourceUri, const QString &destDir);

signals:
  void progress(int percentage, const QString &fileName);
  void finished(bool success, const QString &destPath, const QString &error);
};

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
  Q_PROPERTY(QVariantList backendList READ backendList WRITE setBackendList
                 NOTIFY backendListChanged)
  Q_PROPERTY(QString activeModel READ activeModel WRITE setActiveModel NOTIFY
                 activeModelChanged)
  Q_PROPERTY(QStringList projectList READ projectList NOTIFY projectListChanged)
  Q_PROPERTY(
      QVariantMap hardwareInfo READ hardwareInfo NOTIFY hardwareInfoChanged)

public:
  explicit ConfigManager(QObject *parent = nullptr);
  ~ConfigManager() override;

  // Getters
  unsigned short activeThemeIndex() const { return m_activeThemeIndex; }
  QString lastProject() const { return m_lastProject; }
  QString activeBackend() const { return m_activeBackend; }
  unsigned int contextWindow() const { return m_contextWindow; }
  float modelTemperature() const { return m_modelTemperature; }
  QVariantList backendList() const { return m_backendList; }
  QString activeModel() const { return m_activeModel; }
  QStringList projectList() const { return m_projectList; }

  /// Return the hardware info as a QVariantMap (QML-friendly).
  QVariantMap hardwareInfo() const;

  /// Look up a backend entry by name.  Returns an empty object if not found.
  Q_INVOKABLE QVariantMap backendConfig(const QString &name) const;

  /// Return the list of backend names (for UI dropdowns).
  Q_INVOKABLE QStringList backendNames() const;

  // Setters (Will trigger auto-saving to disk)
  void setActiveThemeIndex(int index);
  void setLastProject(const QString &path);
  Q_INVOKABLE void setActiveBackend(const QString &backend);
  Q_INVOKABLE void setActiveModel(const QString &model);
  void setContextWindow(unsigned int window);
  void setModelTemperature(double temperature);

  /// Replace the entire backend list.
  /// Each entry is a QVariantMap with keys: "name", "url", "api_key".
  void setBackendList(const QVariantList &backends);

  /// Add or update a single backend entry.
  Q_INVOKABLE void setBackend(const QString &name, const QString &url,
                              const QString &apiKey);

  /// Remove a backend by name.
  Q_INVOKABLE void removeBackend(const QString &name);

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

  /// Return the list of absolute PNG paths for a given paper hash,
  /// sorted by page number (e.g. hash_page_001.png, hash_page_002.png …).

  /// Path to the Models directory (contains Embedders/ and Summarizers/).
  Q_INVOKABLE QString modelsPath() const;

  /// Path to the Embedders subdirectory.
  Q_INVOKABLE QString embeddersPath() const;

  /// Path to the Summarizers subdirectory.
  Q_INVOKABLE QString summarizersPath() const;

  /// Copy a GGUF model file to the Summarizers folder in a background
  /// thread.  Emits modelCopyProgress() and modelCopyFinished().
  Q_INVOKABLE void addLocalSummarizer(const QString &sourcePath);

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
  void activeModelChanged();
  void contextWindowChanged();
  void modelTemperatureChanged();
  void backendListChanged();

  /// Progress for model file copy (0–100).
  void modelCopyProgress(int percentage, const QString &fileName);

  /// Model file copy finished.
  /// @param success  true on success.
  /// @param destPath destination path (empty on failure).
  /// @param error    error description (empty on success).
  void modelCopyFinished(bool success, const QString &destPath,
                         const QString &error);

  /// Emitted when a model file was successfully copied.
  void modelCopied();

  void hardwareInfoChanged();

private:
  void refreshProjectList();
  void ensureModelsDirectory();
  void detectAndSaveHardware();
  QVariantMap hardwareToVariantMap(const HardwareInfo &info) const;
  HardwareInfo hardwareFromVariantMap(const QVariantMap &v) const;
  QString getFilePath() const;
  QString hardwareJsonPath() const;
  QString projectsRoot() const;
  QString modelsRoot() const;

  int m_activeThemeIndex = 0;
  QString m_lastProject = "";
  QString m_activeBackend = "";
  QString m_activeModel = "";
  unsigned int m_contextWindow = 4000;
  double m_modelTemperature = 0.1;

  /// Each entry is a QVariantMap: {"name":..., "url":..., "api_key":...}
  QVariantList m_backendList;
  QStringList m_projectList;

  HardwareInfo m_hardwareInfo;

  // Model copy worker
  QThread *m_copyThread = nullptr;
  ModelCopyWorker *m_copyWorker = nullptr;
};
