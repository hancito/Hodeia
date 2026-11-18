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
  Q_PROPERTY(
      QString language READ language WRITE setLanguage NOTIFY languageChanged)
  /// Prompt budget for library queries, in tokens; 0 = auto.
  Q_PROPERTY(int queryContextTokens READ queryContextTokens WRITE
                 setQueryContextTokens NOTIFY queryContextTokensChanged)
  /// Zotero's data folder chosen by the user or found once; empty = search.
  Q_PROPERTY(QString zoteroDataDir READ zoteroDataDir WRITE setZoteroDataDir
                 NOTIFY zoteroDataDirChanged)

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
  QString language() const;
  int queryContextTokens() const { return m_queryContextTokens; }
  QString zoteroDataDir() const { return m_zoteroDataDir; }

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
  void setLanguage(const QString &lang);
  void setQueryContextTokens(int tokens);
  void setZoteroDataDir(const QString &dir);
  Q_INVOKABLE QStringList visualModeList() const;
  VisualMode visualModeEnum() const;
  bool useVisualContext() const;

  /// Replace the entire backend list.
  /// Each entry is a QVariantMap with keys: "name", "url", "api_key".
  void setBackendList(const QVariantList &backends);

  /// Add or update a single backend entry.  @p api is the protocol of a
  /// remote backend: "openai" (OpenAI-compatible) or "anthropic".
  Q_INVOKABLE void setBackend(const QString &name, const QString &url,
                              const QString &apiKey,
                              const QString &api = QStringLiteral("openai"));

  /// Remove a backend by name.
  Q_INVOKABLE void removeBackend(const QString &name);

  // Project Operations (on disk + lastProject only).  QML goes through
  // DatabaseManager, which also closes/reopens the project database and
  // returns a user-facing error string (std::expected does not reach QML).
  std::expected<void, QString> createProject(const QString &projectName);
  std::expected<void, QString> switchToProject(const QString &projectName);
  std::expected<void, QString> deleteProjects(const QStringList &projectNames);
  // The project database must be closed first (its file is deleted).
  std::expected<void, QString> resetActiveProject();
  Q_INVOKABLE QString projectPath(const QString &projectName) const;
  Q_INVOKABLE QString projectDbPath(const QString &projectName) const;
  Q_INVOKABLE QString projectImgPath(const QString &projectName) const;
  /// Delete the page images of the given papers.  Returns an empty string,
  /// or a user-facing message when some images could not be deleted.
  Q_INVOKABLE QString deleteImages(const QStringList &hashList,
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
  void zoteroDataDirChanged();
  void backendListChanged();

  void visualModeChanged();
  void languageChanged();
  void queryContextTokensChanged();

private:
  void refreshProjectList();
  void ensureModelsDirectory();
  /// Copy the bundled model tree (Embedders/ + Summarizers/) from the app
  /// bundle into the data dir, copying only files that are missing. Idempotent:
  /// on a fresh install it seeds every bundled model; on a version bump it
  /// fills in any newly-bundled model without overwriting files the user
  /// already has.
  void seedBundledModels();
  /// If no active model is selected (first run), pick the first available
  /// local model from Models/Summarizers/ so the app works out of the box.
  void selectDefaultModel();
  QString getFilePath() const;
  void reportSaveFailure(const QString &error) const;
  // The settings file exists but could not be parsed nor backed up: never
  // overwrite it during this session.
  bool m_configUnreadable = false;
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
  int m_visualModeIndex = 0; // 0=TextOnly (default), 1=Reduced, 2=Full
  QString m_language = QStringLiteral("en");
  int m_queryContextTokens = 0; // 0 = auto
  QString m_zoteroDataDir;
};
