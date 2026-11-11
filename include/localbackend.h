#pragma once

#include <QObject>
#include <QString>
#include <expected>
#include <optional>
#include <memory>
#include <vector>

struct common_init_result;
struct common_chat_templates;
struct common_params;
struct common_params_sampling;
struct common_chat_prompt;

// MOC scanner needs a concrete type for ggml_backend_dev_t (detectDevices()).
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
  /// System prompt to use instead of systemPrompt when the backend cannot
  /// pass the images (no projector, or it failed to load), so the model is
  /// not told about figures it never receives.  Empty = use systemPrompt.
  QString textOnlySystemPrompt;
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
///      the GPU until the first multimodal request, then stays resident until
///      unloadModel() — this avoids VRAM contention during the model load.
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

  /// Load a GGUF model (and optionally an mmproj) from disk, counting the
  /// formatted prompt tokens with the model's own vocabulary during the
  /// no-alloc probe and sizing the KV cache as prompt_tokens +
  /// responseHeadroom. Returns std::expected<void, QString> — error string on
  /// failure.
  ///
  /// Calls unloadModel() internally if a model is already loaded, so it's
  /// safe to call repeatedly.
  std::expected<void, QString> loadModel(const QString &modelPath,
                                         const PromptPayload &payload,
                                         uint32_t responseHeadroom = 2048);

  /// Returns true if a model is loaded and ready for inference.
  bool isReady() const;

  /// Release the current model, mmproj, chat templates and synchronise
  /// all GPU backends so VRAM is freed before the next load.
  void unloadModel();

  /// Path of the currently loaded model (empty when no model is loaded).
  QString modelPath() const { return m_modelPath; }

  /// High-level entry point: assemble a chat prompt from the payload and
  /// run either text-only or multimodal inference.
  /// \param cancelFlag  optional pointer to a bool the caller sets to request
  ///                    cooperative cancellation; generation stops at the next
  ///                    token boundary and returns the partial output.
  QString summarize(const PromptPayload &payload, bool *cancelFlag = nullptr);

signals:
  /// Emitted when the backend status changes (loading, loaded, errors…).
  void statusUpdated(const QString &msg);

private:
  QString runMultimodalInference(const common_chat_prompt &prompt,
                                 const std::vector<PageImage> &images,
                                 bool *cancelFlag);
  QString runTextInference(const common_chat_prompt &prompt, bool *cancelFlag);

  /// Assemble the user/system prompt with visual markers + text context,
  /// then wrap it in the model's chat template.  `marker` is empty for a
  /// text-only prompt.  nullopt on template failure.
  std::optional<common_chat_prompt>
  buildFormattedPrompt(const PromptPayload &payload, const QString &marker,
                       int32_t maxChars);

  /// Apply the model's Jinja chat template (cached in m_chatTemplates).
  std::optional<common_chat_prompt>
  applyChatTemplate(const QString &systemPrompt, const QString &userMessage);

  /// Resolve a relative or absolute model path to an absolute filesystem path.
  QString resolveModelPath(const QString &modelPath) const;

  /// Search for an mmproj file near the model or in the bundled Mmproj dir.
  QString resolveMmprojPath(const QString &modelPath) const;

  /// Lazy-load the multimodal projector onto the GPU.
  std::expected<void, QString> loadMmproj();
  /// Unload the projector to free its GPU VRAM.
  void unloadMmproj();

  /// Build a common_init_result from params (including hybrid-model FA
  /// re-init).  Never throws; returns nullptr on failure.
  common_init_result_ptr createInitResult(common_params &params);

  /// Detect devices and return {gpu, cpu} for hybrid offloading.
  std::vector<ggml_backend_dev_t> detectDevices();

  /// Unified model loader with hybrid GPU+CPU offloading.
  /// \param payload             optional prompt used to count exact prompt
  ///                            tokens during the no-alloc probe.
  /// \param responseHeadroom    tokens reserved for the generated response
  ///                            when sizing the KV cache from exact prompt
  ///                            tokens.
  common_init_result_ptr loadModelWithDevices(const QString &path,
                                              const QString &mmprojPath,
                                              const PromptPayload *payload,
                                              uint32_t responseHeadroom);

  /// libcommon init result — owns the llama_model + llama_context.
  common_init_result_ptr m_commonInit;

  /// Cached chat-template parser; initialised once from GGUF metadata.
  struct ChatTemplatesDeleter {
    void operator()(common_chat_templates *t) const; // common_chat_templates_free
  };
  std::unique_ptr<common_chat_templates, ChatTemplatesDeleter> m_chatTemplates;

  /// Raw pointers into the model/context owned by m_commonInit.
  struct llama_context *m_llamaCtx = nullptr;
  struct llama_model *m_llamaModel = nullptr;

  bool m_ready = false;
  QString m_modelPath;
  QString m_mmprojPath;

  /// Multimodal context — lazily loaded on first vision request, kept until
  /// unloadModel().
  struct mtmd_context *m_mtmdCtx = nullptr;

  /// Set to true when an mmproj was found next to the model.
  bool m_hasVision = false;
  /// Where loadMmproj puts the projector, decided at model load: the
  /// fitter's choice on the GPU path, any free GPU when the model is on CPU.
  bool m_mmprojUseGpu = false;
  /// Tokens reserved for the response when the KV cache was sized; the
  /// prompt is truncated so this much context stays free for generation.
  uint32_t m_responseHeadroom = 2048;

  common_params_sampling makeSamplingParams() const;
};
