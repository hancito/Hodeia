#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <stdint.h>
#include <vector>

struct llama_context;
struct llama_model;
struct mtmd_context;
struct common_sampler;
struct common_params_sampling;
struct PageImage;

// MOC scanner needs llama_token as a concrete type for tokenToPiece().
#ifndef LLAMA_TOKEN
using llama_token = int32_t;
#endif

/// C++ wrapper around llama.cpp / mtmd that handles tokenization →
/// decoding → sampling → detokenization for both text-only and
/// multimodal prompts.
///
/// Uses the common_sampler mid-level API (sampling.h) which encapsulates
/// the entire sampling chain (penalties, temp, top_k, top_p, min_p,
/// greedy/dist) in a single object built from common_params_sampling.
///
/// The engine does NOT own the llama_context or llama_model — those
/// are managed by the caller (LocalBackend).
class InferenceEngine : public QObject {
  Q_OBJECT

public:
  InferenceEngine(struct llama_context *ctx, struct llama_model *model,
                  const common_params_sampling &samplingParams,
                  QObject *parent = nullptr);
  ~InferenceEngine() override;

  /// Run text-only inference.  `prompt` is the already-template-
  /// formatted chat string (e.g. after applyChatTemplate).
  QString inferText(const QString &prompt);

  /// Run multimodal inference.  `prompt` contains the media marker
  /// (e.g. "<__media__>") at the positions where images should be
  /// injected.  `images` are raw RGB pixel buffers.  `mctx` is the
  /// mtmd multimodal context (loaded alongside the mmproj).
  QString inferMultimodal(const QString &prompt,
                          const std::vector<PageImage> &images,
                          struct mtmd_context *mctx);

  /// Optional stop sequence — generation stops when output ends with this
  /// string.
  void setStopSequence(const QString &seq) { m_stopSequence = seq; }

signals:
  /// Emitted for every generated token piece (for future streaming).
  void tokenGenerated(const QString &piece);

private:
  /// Shared generation loop: prompt is already decoded into the KV
  /// cache; this samples tokens until EOS or a stop condition.
  QString generate();

  /// Clear the KV cache so each inference starts from a clean slate.
  void clearKvCache();

  /// Detokenize a single token, stripping leading-space when the
  /// model uses space-prefix encoding.
  QString tokenToPiece(llama_token tok) const;

  struct llama_context *m_ctx = nullptr;
  struct llama_model *m_model = nullptr;
  const struct llama_vocab *m_vocab = nullptr;
  int32_t m_n_batch = 0;
  QString m_stopSequence;

  /// common_sampler wraps the full sampling chain (sampling.h).
  std::unique_ptr<common_sampler, void (*)(common_sampler *)> m_sampler;
};
