#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class ConfigManager;
class DatabaseManager;
class QNetworkAccessManager;
class QNetworkReply;

/// QML-facing helpers: summary display and PDF export, local model management
/// and remote backend model discovery. Exposed to QML as "helpers".
class Helpers : public QObject {
  Q_OBJECT

  Q_PROPERTY(QStringList localModelList READ localModelList NOTIFY
                 localModelListChanged)

public:
  explicit Helpers(ConfigManager *config, QObject *parent = nullptr);
  ~Helpers() override;

  // ── Summaries ──────────────────────────────────────────────────

  /// The project database, for Zotero's metadata.  Set once from main().
  void setDatabase(DatabaseManager *database) { m_database = database; }

  /// Convert a JSON summary string to HTML (same source used for PDF export).
  /// With `zoteroPriority`, the title, authors, year and journal of paper
  /// `paperHash` come from Zotero where it has them.
  Q_INVOKABLE QString summaryToHtml(const QString &json,
                                    const QString &paperHash = QString(),
                                    bool zoteroPriority = false) const;

  /// Render Markdown (RAG answers) as HTML with paragraph, heading and list
  /// spacing.  Qt's MarkdownText lays blocks out with no margins at all, so
  /// a long answer reads as one dense wall of text.
  Q_INVOKABLE QString markdownToHtml(const QString &markdown) const;

  /// Export a list of JSON summaries as a PDF (a single summary is a
  /// one-paper report). Returns the saved path on success, or an empty
  /// string on failure.  `paperHashes` (same order as `jsonList`) and
  /// `zoteroPriority`: as for summaryToHtml().
  Q_INVOKABLE QString exportPdf(const QStringList &jsonList,
                                const QString &destPath,
                                const QStringList &paperHashes = QStringList(),
                                bool zoteroPriority = false) const;

  /// Return the list of absolute PNG paths for a given paper hash,
  /// sorted by page number.
  Q_INVOKABLE QStringList getImagesForHash(const QString &paperHash,
                                           const QString &imgPath) const;

  /// Convert a local file path to a file: URL string (cross-platform).
  /// Uses QUrl::fromLocalFile so Windows drive letters, forward/back slashes,
  /// and spaces are all handled correctly.
  Q_INVOKABLE QString localFileUrl(const QString &path) const;

  // ── Local models ───────────────────────────────────────────────

  /// Local summarizer models: Summarizers sub-folders holding a model .gguf
  /// (not just an mmproj).
  QStringList localModelList() const;

  /// Copy a GGUF model file, plus an optional mmproj file, into its own
  /// Summarizers sub-folder on a background thread. Paths may be local paths
  /// or file: URLs. Emits modelCopyProgress() then modelCopyFinished().
  Q_INVOKABLE void addLocalSummarizer(const QString &modelPath,
                                      const QString &mmprojPath);

  /// Delete one or more local model folders from the Summarizers directory.
  /// Returns an empty string, or a user-facing message naming the models
  /// that could not be fully deleted.
  Q_INVOKABLE QString deleteLocalModels(const QStringList &folderNames);

  // ── Backends ───────────────────────────────────────────────────

  /// Check a remote backend and retrieve its available models; on success
  /// the backend is saved and made active. Emits modelsChecked().
  /// @p api: "openai" (OpenAI-compatible) or "anthropic".
  Q_INVOKABLE void checkRemoteModels(const QString &name, const QString &apiUrl,
                                     const QString &apiKey,
                                     const QString &api = QStringLiteral("openai"));

  /// Load available models for the currently active backend (local or remote).
  /// Emits modelsChecked() with the result.
  Q_INVOKABLE void loadActiveBackendModels();

  /// The agent profiles for the Backends window (see AgentBackend::profiles):
  /// the ones that can start on this computer first.
  Q_INVOKABLE QVariantList agentProfiles() const;
  /// Check an agent backend (Claude Code, Codex, Qwen Code...) by listing its
  /// models; on success it is saved as @p name and made active.  @p command
  /// replaces the profile's when not empty.  Emits modelsChecked().
  Q_INVOKABLE void checkAgentModels(const QString &name, const QString &agentId,
                                    const QString &command);

signals:
  void modelsChecked(bool success, const QStringList &models,
                     const QString &error);
  void modelCopyProgress(int percentage, const QString &fileName);
  void modelCopyFinished(bool success, const QString &destPath,
                         const QString &error);
  void localModelListChanged();

private:
  /// The models of an agent backend, read on a pool thread (the agent may be
  /// started once).  Saves the backend as @p saveAs on success.
  void fetchAgentModels(const QVariantMap &config, const QString &saveAs);
  int m_modelsRequest = 0; ///< bumped by each model listing: stale ones drop
  /// GET <apiUrl>/models. If @p saveAs is set, the backend is saved under
  /// that name and made active once the server answered.
  void fetchRemoteModels(const QString &apiUrl, const QString &apiKey,
                         const QString &api, const QString &saveAs);
  /// Emit modelsChecked() for a finished reply; true on success.
  bool handleModelsReply(QNetworkReply *reply);
  void cancelPendingReply();
  void refreshLocalModelList();

  ConfigManager *m_config = nullptr;
  DatabaseManager *m_database = nullptr;
  QNetworkAccessManager *m_networkManager = nullptr;
  QNetworkReply *m_currentReply = nullptr;
  QStringList m_localModelList;
};
