#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class ConfigManager;
class QNetworkAccessManager;
class QNetworkReply;

/// Paper-level summary produced by the LLM.
struct PaperSummary {
  QString title;
  QStringList authors;
  QString journal;
  int year = 0;
  QStringList topics;
  QString researchObjective;
  QString methodology;
  QStringList coreFindingsAndSignificance;
  QStringList criticalLimitations;
  QString citableSummary;
};

/// General-purpose helper for model discovery, summary formatting, and
/// PDF export. Exposed to QML as "helpers".
class Helpers : public QObject {
  Q_OBJECT

  Q_PROPERTY(QStringList localModelList READ localModelList NOTIFY
                 localModelListChanged)

public:
  explicit Helpers(ConfigManager *config, QObject *parent = nullptr);
  ~Helpers() override;

  // ── Summary parsing & formatting (static) ──────────────────────

  /// Parse a raw LLM JSON response into a PaperSummary.
  static PaperSummary parseSummaryJson(const QString &json);

  /// Convert a PaperSummary to HTML (used for both the QML display and PDF).
  static QString toHtml(const PaperSummary &summary);

  // ── QML-invokable ──────────────────────────────────────────────

  /// Convert a JSON summary string to HTML (same source used for PDF export).
  Q_INVOKABLE QString summaryToHtml(const QString &json) const;

  /// Export a list of JSON summaries as a PDF, one paper per page (a single
  /// summary is a one-paper report). Returns the saved path on success, or an
  /// empty string on failure.
  Q_INVOKABLE QString exportPdf(const QStringList &jsonList,
                                const QString &destPath) const;

  /// List folder names of local summarizer models.
  QStringList localModelList();

  /// List .gguf model files in the local Summarizers directory.
  QStringList listLocalModels();

  /// Return the list of absolute PNG paths for a given paper hash,
  /// sorted by page number.
  Q_INVOKABLE QStringList getImagesForHash(const QString &paperHash,
                                           const QString &imgPath) const;

  /// Convert a local file path to a file: URL string (cross-platform).
  /// Uses QUrl::fromLocalFile so Windows drive letters, forward/back slashes,
  /// and spaces are all handled correctly.
  Q_INVOKABLE QString localFileUrl(const QString &path) const;

  /// Check a remote backend and retrieve its available models.
  /// Emits modelsChecked() with the result.
  Q_INVOKABLE void checkRemoteModels(const QString &name, const QString &apiUrl,
                                     const QString &apiKey);

  /// Load available models for the currently active backend (local or remote).
  /// Emits modelsChecked() with the result.
  Q_INVOKABLE void loadActiveBackendModels();

  /// Copy a GGUF model file to the Summarizers folder in a background
  /// thread. Emits modelCopyProgress() and modelCopyFinished().
  Q_INVOKABLE void addLocalSummarizer(const QString &sourcePath);

  /// Copy a GGUF model file together with an optional mmproj file to
  /// the Summarizers folder using parallel background threads.
  Q_INVOKABLE void addLocalSummarizerWithMmproj(const QString &modelPath,
                                                const QString &mmprojPath);

  /// Delete one or more local model folders from the Summarizers directory.
  Q_INVOKABLE void deleteLocalModels(const QStringList &folderNames);

signals:
  void modelsChecked(bool success, const QStringList &models,
                     const QString &error);
  void modelCopyProgress(int percentage, const QString &fileName);
  void modelCopyFinished(bool success, const QString &destPath,
                         const QString &error);
  void modelCopied();
  void localModelListChanged();

private:
  void fetchRemoteModels(const QString &apiUrl, const QString &apiKey);
  void handleModelsReply(QNetworkReply *reply);
  void refreshLocalModelList();

  ConfigManager *m_config = nullptr;
  QNetworkAccessManager *m_networkManager = nullptr;
  QNetworkReply *m_currentReply = nullptr;
  QStringList m_localModelList;
};
