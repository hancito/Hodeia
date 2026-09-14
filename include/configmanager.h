#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <expected>
#include <qtmetamacros.h>

/// Visual context mode for summarization.
enum class VisualMode : int { TextOnly = 0, Reduced = 1, Full = 2 };

class ConfigManager : public QObject {
  Q_OBJECT

  // Expose your configuration fields directly to QML
  Q_PROPERTY(unsigned short activeThemeIndex READ activeThemeIndex WRITE
                 setActiveThemeIndex NOTIFY activeThemeIndexChanged)
  Q_PROPERTY(QString lastProject READ lastProject WRITE setLastProject NOTIFY
                 lastProjectChanged)
  Q_PROPERTY(QString activeBackend READ activeBackend WRITE setActiveBackend
                 NOTIFY activeBackendChanged)
  Q_PROPERTY(QVariantList backendList READ backendList WRITE setBackendList
                 NOTIFY backendListChanged)
  Q_PROPERTY(QString activeModel READ activeModel WRITE setActiveModel NOTIFY
                 activeModelChanged)
  Q_PROPERTY(QString openAlexApiKey READ openAlexApiKey WRITE setOpenAlexApiKey
                 NOTIFY openAlexApiKeyChanged)
  Q_PROPERTY(QStringList projectList READ projectList NOTIFY projectListChanged)
  Q_PROPERTY(QString visualMode READ visualMode WRITE setVisualMode NOTIFY
                 visualModeChanged)

public:
  explicit ConfigManager(QObject *parent = nullptr);
  ~ConfigManager() override;

  // Getters
  unsigned short activeThemeIndex() const { return m_activeThemeIndex; }
  QString lastProject() const { return m_lastProject; }
  QString activeBackend() const { return m_activeBackend; }
  QVariantList backendList() const { return m_backendList; }
  QString activeModel() const { return m_activeModel; }
  QString openAlexApiKey() const { return m_openAlexApiKey; }
  QStringList projectList() const { return m_projectList; }
  QString visualMode() const;

  /// Look up a backend entry by name.  Returns an empty object if not found.
  Q_INVOKABLE QVariantMap backendConfig(const QString &name) const;

  /// Return the list of backend names (for UI dropdowns).
  Q_INVOKABLE QStringList backendNames() const;

  // Setters (Will trigger auto-saving to disk)
  void setActiveThemeIndex(int index);
  void setLastProject(const QString &path);
  Q_INVOKABLE void setActiveBackend(const QString &backend);
  Q_INVOKABLE void setActiveModel(const QString &model);
  void setOpenAlexApiKey(const QString &key);
  void setVisualMode(const QString &mode);
  Q_INVOKABLE QStringList visualModeList() const;
  VisualMode visualModeEnum() const;
  bool useVisualContext() const;

  /// Replace the entire backend list.
  /// Each entry is a QVariantMap with keys: "name", "url", "api_key".
  void setBackendList(const QVariantList &backends);

  /// Add or update a single backend entry.
  Q_INVOKABLE void setBackend(const QString &name, const QString &url,
                              const QString &apiKey);

  /// Remove a backend by name.
  Q_INVOKABLE void removeBackend(const QString &name);

  // Project Operations
  Q_INVOKABLE std::expected<void, QString>
  createProject(const QString &projectName);
  Q_INVOKABLE std::expected<void, QString>
  switchToProject(const QString &projectName);
  Q_INVOKABLE std::expected<void, QString>
  deleteProjects(const QStringList &projectNames);
  Q_INVOKABLE std::expected<void, QString> resetActiveProject();
  Q_INVOKABLE QString projectPath(const QString &projectName) const;
  Q_INVOKABLE QString projectDbPath(const QString &projectName) const;
  Q_INVOKABLE QString projectImgPath(const QString &projectName) const;
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
  void openAlexApiKeyChanged();
  void backendListChanged();

  /// Progress for model file copy (0–100).
  /// Emitted by Helpers but forwarded here for QML convenience.
  void modelCopyProgress(int percentage, const QString &fileName);

  /// Model file copy finished (emitted by Helpers, forwarded here).
  void modelCopyFinished(bool success, const QString &destPath,
                         const QString &error);

  /// Emitted when a model file was successfully copied.
  void modelCopied();

  void visualModeChanged();

private:
  void refreshProjectList();
  void ensureModelsDirectory();
  /// Copy the default embedding model into the data dir if it is missing.
  /// Runs once at startup so SqliteVecManager can find it on a fresh install.
  void seedDefaultEmbedder();
  QString getFilePath() const;
  QString projectsRoot() const;
  QString modelsRoot() const;

  int m_activeThemeIndex = 0;
  QString m_lastProject = "";
  QString m_activeBackend = "";
  QString m_activeModel = "";
  QString m_openAlexApiKey = "";

  /// Each entry is a QVariantMap: {"name":..., "url":..., "api_key":...}
  QVariantList m_backendList;
  QStringList m_projectList;
  QString m_visualMode = "Reduced (figures/tables)";
};
