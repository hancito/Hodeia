#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <stdint.h>
#include <string>
#include <vector>

struct llama_context;
struct llama_model;
struct mtmd_context;
struct llama_vocab;
struct common_chat_prompt;
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
                  common_params_sampling samplingParams,
                  QObject *parent = nullptr);
  ~InferenceEngine() override;

  /// Run text-only inference on a chat-template-rendered prompt.  Template
  /// parts are tokenized with special-token parsing, user-input parts
  /// (the document text) without it, so text inside a PDF cannot inject
  /// control tokens such as <end_of_turn>.
  QString inferText(const common_chat_prompt &prompt);

  /// Run multimodal inference.  The user-input parts of `prompt` contain the
  /// media marker (mtmd_get_marker) once per image, in order; `images` are
  /// raw RGB pixel buffers.  `mctx` is the loaded mtmd (mmproj) context.
  QString inferMultimodal(const common_chat_prompt &prompt,
                          const std::vector<PageImage> &images,
                          struct mtmd_context *mctx);

  /// Tokenize a rendered prompt exactly as inferText() does.
  static std::vector<llama_token> tokenizePrompt(const struct llama_vocab *vocab,
                                                 const common_chat_prompt &prompt);

  /// Number of tokens (KV cells) a multimodal prompt takes, text + images,
  /// tokenized exactly as inferMultimodal() does.  -1 on error.
  static int32_t countMultimodalTokens(struct mtmd_context *mctx,
                                       const common_chat_prompt &prompt,
                                       const std::vector<PageImage> &images);

  /// Optional cooperative-cancellation flag. When the pointed-to value is
  /// true, generation stops at the next token boundary and the partial
  /// output is returned. The flag is owned and cleared by the caller
  /// (e.g. the worker's m_cancelled); this engine only reads it.
  void setCancelFlag(bool *flag) { m_cancelFlag = flag; }

signals:
  /// Emitted for every generated token piece (for future streaming).
  void tokenGenerated(const QString &piece);

private:
  /// Shared generation loop: prompt is already decoded into the KV
  /// cache up to position `nPast` (exclusive) and occupies `nCells` KV
  /// cells; this samples tokens until EOS, cancellation or the token
  /// budget (n_ctx - nCells). The two differ for M-RoPE models, where an
  /// image advances the position by less than its token count.
  /// (llama_pos is int32_t.)
  QString generate(int32_t nPast, int32_t nCells);

  /// Clear the KV cache so each inference starts from a clean slate.
  void clearKvCache();

  /// Detokenize a single token to raw UTF-8 bytes. The result may be an
  /// incomplete UTF-8 sequence, so callers must accumulate before decoding.
  std::string tokenToPiece(llama_token tok) const;

  struct llama_context *m_ctx = nullptr;
  struct llama_model *m_model = nullptr;
  const struct llama_vocab *m_vocab = nullptr;
  int32_t m_n_batch = 0;
  bool *m_cancelFlag = nullptr;

  /// common_sampler wraps the full sampling chain (sampling.h).
  std::unique_ptr<common_sampler, void (*)(common_sampler *)> m_sampler;
};
