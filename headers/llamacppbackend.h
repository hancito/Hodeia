#pragma once

#include <QObject>
#include <QString>

/// Llama.cpp backend — pure inference engine.
///
/// Responsibilities: load a GGUF chat model, run generation,
/// return raw text output.  Nothing else (no DB, no prompt building,
/// no JSON parsing).  The Summarizer orchestrates everything above.
class LlamaCppBackend : public QObject {
  Q_OBJECT

public:
  explicit LlamaCppBackend(QObject *parent = nullptr);
  ~LlamaCppBackend() override;

  /// Load a GGUF model for chat completion.
  /// modelPath points to a GGUF file; empty string uses the default
  /// Summarizers/ model.  Returns true on success.
  bool loadModel(const QString &modelPath = QString());

  /// Whether a model is currently loaded.
  bool isReady() const;

  /// Unload the model and free GPU/CPU memory.
  void unloadModel();

  /// Currently loaded model path (empty if none).
  QString modelPath() const { return m_modelPath; }

  /// Run the LLM generation and return the raw text output.
  /// Blocks until generation is complete.  Not thread-safe for
  /// concurrent callers — serialise from a single worker thread.
  QString generate(const QString &systemPrompt, const QString &userPrompt);

signals:
  void statusUpdated(const QString &msg);

private:
  QString resolveModelPath(const QString &modelPath) const;

  // Llama.cpp handles (opaque).
  struct llama_context *m_llamaCtx = nullptr;
  struct llama_model *m_llamaModel = nullptr;
  bool m_ready = false;
  QString m_modelPath;

  // Generation params
  static constexpr int32_t MaxPromptTokens = 8192;
  static constexpr int32_t MaxGenTokens = 4096;
  static constexpr float Temperature = 0.1f;
  static constexpr int32_t TopK = 40;
  static constexpr float TopP = 0.9f;
};
