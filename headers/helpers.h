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
  QString coreFindingsAndSignificance;
  QString criticalLimitations;
  QString citableSummary;
};

namespace SummaryHelpers {
/// Parse LLM JSON response into a PaperSummary struct.
PaperSummary parseSummaryJson(const QString &json);

/// Convert a PaperSummary to a readable Markdown string.
QString toMarkdown(const PaperSummary &summary);

/// Convert a PaperSummary to a LaTeX snippet.
QString toLatex(const PaperSummary &summary);
} // namespace SummaryHelpers

/// Singleton-like helper that manages model discovery for both local
/// (Llama.cpp .gguf) and remote (OpenAI-compatible API) backends.
/// Exposed to QML as "modelHelper".
class ModelHelper : public QObject {
  Q_OBJECT

public:
  explicit ModelHelper(ConfigManager *config, QObject *parent = nullptr);
  ~ModelHelper() override;

public slots:
  /// Convert a JSON summary string to Markdown.
  Q_INVOKABLE QString summaryToMarkdown(const QString &json) const;

  /// Convert a JSON summary string to LaTeX.
  Q_INVOKABLE QString summaryToLatex(const QString &json) const;

  /// List .gguf model files in the local Summarizers directory.
  QStringList listLocalModels();

  /// Return the list of absolute PNG paths for a given paper hash,
  /// sorted by page number (e.g. hash_page_001.png, hash_page_002.png …).
  Q_INVOKABLE QStringList getImagesForHash(const QString &paperHash,
                                           const QString &imgPath) const;

  /// Check a remote backend and retrieve its available models.
  /// Emits modelsChecked() with the result.
  void checkRemoteModels(const QString &name, const QString &apiUrl,
                         const QString &apiKey);

  /// Load available models for the currently active backend (local or remote).
  /// Emits modelsChecked() with the result.
  void loadActiveBackendModels();

signals:
  /// Emitted after a model discovery operation completes.
  /// \param success  true if the operation succeeded and models are available.
  /// \param models   list of model names / file names.
  /// \param error    error message if success is false.
  void modelsChecked(bool success, const QStringList &models,
                     const QString &error);

private:
  void fetchRemoteModels(const QString &apiUrl, const QString &apiKey);
  void handleModelsReply(QNetworkReply *reply);

  ConfigManager *m_config = nullptr;
  QNetworkAccessManager *m_networkManager = nullptr;
  QNetworkReply *m_currentReply = nullptr;
};
