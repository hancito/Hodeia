#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <vector>

// The MOC scanner needs to know what llama_token is before it parses
// the private method tokenToPiece().  When llama.h is already included
// (during normal compilation) this guard is a no-op.
#ifndef LLAMA_H
#include <stdint.h>
typedef int32_t llama_token;
#endif

struct llama_context;
struct llama_model;
struct mtmd_context;

/// Raw RGB image — no dependency on any external library.
struct RawImage {
  int width;
  int height;
  std::vector<unsigned char> data; ///< RGB888, width*height*3 bytes
};

/// Sampling parameters for the inference engine.
struct InferenceParams {
  int32_t max_tokens = 4096;
  float temperature = 0.1f;
  int32_t top_k = 40;
  float top_p = 0.9f;
  float min_p = 0.05f;
  float penalty_repeat = 1.1f;
  int32_t penalty_last_n = 64;
  uint32_t seed = 0;
  int32_t n_threads = 0; // 0 = idealThreadCount()
  QString stop_sequence; // optional early-stop string
};

/// C++ wrapper around llama.cpp / mtmd that handles tokenization →
/// decoding → sampling → detokenization for both text-only and
/// multimodal prompts.
///
/// The engine does NOT own the llama_context or llama_model — those
/// are managed by the caller (LlamaChatBackend).  Callers pass a
/// fully initialised context; the engine uses it for one inference
/// pass (prompt + generation) and returns.
///
/// All llama.h / mtmd.h details are hidden inside this class; callers
/// never touch those headers directly.
class LlamaInferenceEngine : public QObject {
  Q_OBJECT

public:
  LlamaInferenceEngine(struct llama_context *ctx, struct llama_model *model,
                       const InferenceParams &params,
                       QObject *parent = nullptr);
  ~LlamaInferenceEngine() override;

  /// Run text-only inference.  `prompt` is the already-template-
  /// formatted chat string (e.g. after applyChatTemplate).
  QString inferText(const QString &prompt);

  /// Run multimodal inference.  `prompt` contains the media marker
  /// (e.g. "<__media__>") at the positions where images should be
  /// injected.  `images` are raw RGB pixel buffers.  `mctx` is the
  /// mtmd multimodal context (loaded alongside the mmproj).
  QString inferMultimodal(const QString &prompt,
                          const std::vector<RawImage> &images,
                          struct mtmd_context *mctx);

signals:
  /// Emitted for every generated token piece (for future streaming).
  void tokenGenerated(const QString &piece);

private:
  /// Build the llama_sampler_chain from InferenceParams.
  void buildSampler();
  void freeSampler();

  /// Shared generation loop: prompt is already decoded into the KV
  /// cache; this samples tokens until EOS or a stop condition.
  QString generate();

  /// Check whether the accumulated output ends with stop_sequence.
  bool shouldStop(const QString &output) const;

  /// Detokenize a single token, stripping leading-space when the
  /// model uses space-prefix encoding.
  QString tokenToPiece(llama_token tok) const;

  struct llama_context *m_ctx = nullptr;
  struct llama_model *m_model = nullptr;
  const struct llama_vocab *m_vocab = nullptr;
  struct llama_sampler *m_chain = nullptr;
  InferenceParams m_params;
  mutable std::vector<char> m_pieceBuf;
};
