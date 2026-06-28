#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class ConfigManager;
class RemoteBackend;

/// Singleton-like helper that manages model discovery for both local
/// (Llama.cpp .gguf) and remote (OpenAI-compatible API) backends.
/// Exposed to QML as "modelHelper".
class ModelHelper : public QObject {
  Q_OBJECT

public:
  explicit ModelHelper(ConfigManager *config, QObject *parent = nullptr);

public slots:
  /// List .gguf model files in the local Summarizers directory.
  QStringList listLocalModels();

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
  RemoteBackend *m_remoteBackend = nullptr;
  ConfigManager *m_config = nullptr;
  QThread *m_workerThread = nullptr;
};
