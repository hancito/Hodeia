#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

/// One page image as raw RGB pixels (no base64).
struct PageImage {
  int width;
  int height;
  std::vector<unsigned char> data; ///< RGB888, width*height*3 bytes
};

/// Payload passed to the LlamaChatBackend for summarization.
struct PromptPayload {
  QString systemPrompt;
  QString userMessage;
  QString textContext;
  std::vector<PageImage> visualContext; ///< raw RGB page images
};

/// Local llama.cpp backend for chat completion (summarization).
/// Supports multimodal models via libmtmd (images) when an mmproj is
/// found next to the model file.
class LlamaChatBackend : public QObject {
  Q_OBJECT

public:
  explicit LlamaChatBackend(QObject *parent = nullptr);
  ~LlamaChatBackend() override;

  bool loadModel(const QString &modelPath = QString());
  bool isReady() const;
  void unloadModel();
  QString modelPath() const { return m_modelPath; }

  /// Run summarization with a rich payload (text + images).
  QString summarize(const PromptPayload &payload);

  /// Run raw chat completion from system + user prompts.
  QString generate(const QString &systemPrompt, const QString &userPrompt);

signals:
  void statusUpdated(const QString &msg);

private:
  QString resolveModelPath(const QString &modelPath) const;
  QString resolveMmprojPath(const QString &modelPath) const;
  QString buildFormattedPrompt(const PromptPayload &payload, int32_t maxChars);
  QString runInference(const QString &formattedText,
                       const std::vector<PageImage> &pageImages);
  struct llama_sampler *createSampler() const;
  QString generateTokens(struct llama_sampler *smpl);

  struct llama_context *m_llamaCtx = nullptr;
  struct llama_model *m_llamaModel = nullptr;
  bool m_ready = false;
  QString m_modelPath;
  QString m_mmprojPath;

  // Multimodal (mtmd) state
  struct mtmd_context *m_mtmdCtx = nullptr;
  bool m_hasVision = false;

  static constexpr int32_t MaxPromptTokens = 8192;
  static constexpr int32_t MaxGenTokens = 4096;
  static constexpr float Temperature = 0.1f;
  static constexpr int32_t TopK = 40;
  static constexpr float TopP = 0.9f;
};
