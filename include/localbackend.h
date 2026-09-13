#pragma once

#include <QObject>
#include <QString>
#include <expected>
#include <memory>
#include <vector>

struct common_init_result;
struct common_chat_templates;
struct common_params;
struct common_params_sampling;

// MOC scanner needs a type for detectBestDevice() return value.
#ifndef GGML_BACKEND_H
struct ggml_backend_device {};
using ggml_backend_dev_t = ggml_backend_device *;
#endif

/// Opaque returned by common_init_from_params; wrapped in a unique_ptr so
/// the model + context are automatically freed when the backend unloads.
using common_init_result_ptr = std::unique_ptr<common_init_result>;

/// One scanned page as raw RGB pixels (no base64 encoding).
/// Used to pass image data from the UI layer to the inference engine without
/// allocating intermediate Qt image objects on the C API boundary.
struct PageImage {
  int width;
  int height;
  std::vector<unsigned char> data; ///< RGB888, width*height*3 bytes

  /// Build a PageImage by COPYING the pixels out of a QImage so the result
  /// is self-contained and does not alias the (possibly temporary) source
  /// image's buffer.
  static PageImage fromQImage(const QImage &img);
};

/// One page: OCR text paired with an optional rendered page image.
/// Used to build interleaved prompts: <--image--> <--text--> per page.
struct PageBlock {
  int pageNumber;          ///< 1-based page number
  QString text;            ///< OCR text for this page
  PageImage visualContext; ///< page image (empty data = no image)
};

/// All inputs needed to produce a summarization response.
/// The backend assembles these fields into a formatted chat prompt, applies
/// the model's chat template, and feeds it to the inference engine.
struct PromptPayload {
  QString systemPrompt;
  QString userMessage;
  QString textContext;
  std::vector<PageImage> visualContext; ///< raw RGB page images
  std::vector<PageBlock> pageBlocks;    ///< interleaved page-by-page (takes
                                        ///< precedence when non-empty)
};

/// Local llama.cpp backend for chat completion (summarization).
///
/// Architecture overview:
///   1. Models are loaded through libcommon (common_init_from_params) which
///      handles device probing, flash-attention selection and VRAM fitting.
///   2. The mmproj (multimodal projector) is **lazy-loaded**: it stays off
///      the GPU until the first multimodal request, then is unloaded
///      immediately after — this avoids VRAM contention with the main model.
///   3. Chat templates are parsed once from GGUF metadata and cached for
///      the lifetime of the loaded model.
///   4. All heavy lifting (tokenize → decode → sample) is delegated to
///      the inference engine; this class only orchestrates state.
///
/// Ownership: common_init_result (m_commonInit) owns the llama_model and
/// llama_context.  We keep raw pointers for convenience but the unique_ptr
/// guarantees cleanup when the backend is destroyed or unloadModel() runs.
class LocalBackend : public QObject {
  Q_OBJECT

public:
  /// Construct the backend and initialise the global llama.cpp runtime
  /// (ggml backends + thread pool) exactly once via a static flag.
  explicit LocalBackend(QObject *parent = nullptr);
  ~LocalBackend() override;

  /// Load a GGUF model (and optionally an mmproj) from disk.
  /// Returns std::expected<void, QString> — error string on failure.
  ///
  /// \param estimatedPromptTokens  number of tokens the upcoming prompt will
  ///                                consume (prompt + generation).  Pass 0 to
  ///                                use the model's trained context size.  The
  ///                                fitter uses this to size the KV cache
  ///                                realistically, avoiding wasted VRAM on
  ///                                unused context slots.
  ///
  /// Calls unloadModel() internally if a model is already loaded, so it's
  /// safe to call repeatedly.
  std::expected<void, QString> loadModel(const QString &modelPath = QString(),
                                         uint32_t estimatedPromptTokens = 0);

  /// Returns true if a model is loaded and ready for inference.
  bool isReady() const;

  /// Release the current model, mmproj, chat templates and synchronise
  /// all GPU backends so VRAM is freed before the next load.
  void unloadModel();

  /// Path of the currently loaded model (empty when no model is loaded).
  QString modelPath() const { return m_modelPath; }

  /// High-level entry point: assemble a chat prompt from the payload and
  /// run either text-only or multimodal inference.
  QString summarize(const PromptPayload &payload);

  /// Convenience wrapper around summarize() for simple text-only calls.
  QString generate(const QString &systemPrompt, const QString &userPrompt);

signals:
  /// Emitted when the backend status changes (loading, loaded, errors…).
  void statusUpdated(const QString &msg);

private:
  QString runMultimodalInference(const QString &formattedText,
                                 const std::vector<PageImage> &images);
  QString runTextInference(const QString &formattedText);

  /// Assemble the user/system prompt with visual markers + text context,
  /// then wrap it in the model's chat template.
  QString buildFormattedPrompt(const PromptPayload &payload, int32_t maxChars);

  /// Apply the model's Jinja chat template (cached in m_chatTemplates).
  QString applyChatTemplate(const QString &systemPrompt,
                            const QString &userMessage);

  /// Resolve a relative or absolute model path to an absolute filesystem path.
  QString resolveModelPath(const QString &modelPath) const;

  /// Search for an mmproj file near the model or in the bundled Mmproj dir.
  QString resolveMmprojPath(const QString &modelPath) const;

  /// Lazy-load the multimodal projector onto the GPU.
  std::expected<void, QString> loadMmproj();
  /// Unload the projector to free its GPU VRAM.
  void unloadMmproj();

  /// Build and probe a common_init_result from params (including hybrid FA
  /// re-init).  Returns nullptr on failure.
  common_init_result_ptr createInitResult(common_params &params,
                                          bool hasMmprojOnGpu);

  /// Detect devices and return {gpu, cpu} for hybrid offloading.
  std::vector<ggml_backend_dev_t> detectDevices();

  /// Unified model loader with hybrid GPU+CPU offloading.
  /// \param estimatedCtxTokens  total tokens the KV cache needs to hold
  ///                            (prompt + generation).  Pass 0 for default.
  common_init_result_ptr
  loadModelWithDevices(const QString &path, const QString &mmprojPath,
                       const std::vector<ggml_backend_dev_t> &devices,
                       const char *primaryName,
                       uint32_t estimatedCtxTokens = 0);

  /// libcommon init result — owns the llama_model + llama_context.
  common_init_result_ptr m_commonInit;

  /// Cached chat-template parser; initialised once from GGUF metadata.
  common_chat_templates *m_chatTemplates = nullptr;

  /// Raw pointers into the model/context owned by m_commonInit.
  struct llama_context *m_llamaCtx = nullptr;
  struct llama_model *m_llamaModel = nullptr;

  bool m_ready = false;
  QString m_modelPath;
  QString m_mmprojPath;

  /// Multimodal context — lazily loaded on first vision request, unloaded
  /// after each inference to keep VRAM available for the main model.
  struct mtmd_context *m_mtmdCtx = nullptr;

  /// Set to true when an mmproj was found next to the model.
  bool m_hasVision = false;
  bool m_isIntegratedGPU = false;
  int32_t m_gpuLayers = 0; // actual n_gpu_layers used on last load

  common_params_sampling makeSamplingParams() const;
};
