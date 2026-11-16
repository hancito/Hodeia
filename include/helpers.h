#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class ConfigManager;
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

  /// Convert a JSON summary string to HTML (same source used for PDF export).
  Q_INVOKABLE QString summaryToHtml(const QString &json) const;

  /// Render Markdown (RAG answers) as HTML with paragraph, heading and list
  /// spacing.  Qt's MarkdownText lays blocks out with no margins at all, so
  /// a long answer reads as one dense wall of text.
  Q_INVOKABLE QString markdownToHtml(const QString &markdown) const;

  /// Export a list of JSON summaries as a PDF (a single summary is a
  /// one-paper report). Returns the saved path on success, or an empty
  /// string on failure.
  Q_INVOKABLE QString exportPdf(const QStringList &jsonList,
                                const QString &destPath) const;

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
  Q_INVOKABLE void checkRemoteModels(const QString &name, const QString &apiUrl,
                                     const QString &apiKey);

  /// Load available models for the currently active backend (local or remote).
  /// Emits modelsChecked() with the result.
  Q_INVOKABLE void loadActiveBackendModels();

signals:
  void modelsChecked(bool success, const QStringList &models,
                     const QString &error);
  void modelCopyProgress(int percentage, const QString &fileName);
  void modelCopyFinished(bool success, const QString &destPath,
                         const QString &error);
  void localModelListChanged();

private:
  /// GET <apiUrl>/models. If @p saveAs is set, the backend is saved under
  /// that name and made active once the server answered.
  void fetchRemoteModels(const QString &apiUrl, const QString &apiKey,
                         const QString &saveAs);
  /// Emit modelsChecked() for a finished reply; true on success.
  bool handleModelsReply(QNetworkReply *reply);
  void cancelPendingReply();
  void refreshLocalModelList();

  ConfigManager *m_config = nullptr;
  QNetworkAccessManager *m_networkManager = nullptr;
  QNetworkReply *m_currentReply = nullptr;
  QStringList m_localModelList;
};
