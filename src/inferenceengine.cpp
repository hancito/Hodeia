#include "headers/inferenceengine.h"
#include "headers/localbackend.h"

#include "llama.h"
#include "mtmd-helper.h"
#include "mtmd.h"

#include <QDebug>
#include <QThread>
#include <algorithm>
#include <vector>

/// Construct the engine and build the sampler chain.
/// The engine captures raw pointers to the context/model (owned by the
/// backend) and initialises a sampler chain from the inference params.
InferenceEngine::InferenceEngine(struct llama_context *ctx,
                                 struct llama_model *model,
                                 const InferenceParams &params, QObject *parent)
    : QObject(parent), m_ctx(ctx), m_model(model),
      m_vocab(llama_model_get_vocab(model)), m_n_batch(llama_n_batch(ctx)),
      m_params(params) {
  if (m_params.n_threads == 0) {
    m_params.n_threads = QThread::idealThreadCount();
  }
  m_pieceBuf.resize(32); // pre-allocate detokenization buffer
  buildSampler();
}

// ── Sampler chain construction ────────────────────────────────────

/// Build a pipeline of samplers that transform raw logits into a token
/// selection.  The chain runs in this order:
///
///   penalties → temp → top_k → top_p → min_p → greedy/dist
///
/// Each stage modifies the logit distribution before passing it to the
/// next.  The final stage (greedy or dist) picks the actual token.
/// This pipeline approach lets us combine multiple sampling strategies
/// without any single one dominating.
void InferenceEngine::buildSampler() {
  auto chainParams = llama_sampler_chain_default_params();
  m_chain = llama_sampler_chain_init(chainParams);

  // Penalty sampler (repetition, frequency, presence)
  if (m_params.penalty_repeat > 1.0f || m_params.penalty_last_n > 0) {
    auto penParams = llama_sampler_init_penalties(
        m_params.penalty_last_n, m_params.penalty_repeat, 0.0f, 0.0f);
    llama_sampler_chain_add(m_chain, penParams);
  }

  // Temperature (softmax smoothing)
  llama_sampler_chain_add(m_chain,
                          llama_sampler_init_temp(m_params.temperature));

  // Top-k
  if (m_params.top_k > 0) {
    llama_sampler_chain_add(m_chain, llama_sampler_init_top_k(m_params.top_k));
  }

  // Top-p
  if (m_params.top_p < 1.0f) {
    llama_sampler_chain_add(m_chain,
                            llama_sampler_init_top_p(m_params.top_p, 1));
  }

  // Min-p
  if (m_params.min_p > 0.0f) {
    llama_sampler_chain_add(m_chain,
                            llama_sampler_init_min_p(m_params.min_p, 1));
  }

  // Final selector (greedy when temp == 0, dist otherwise)
  if (m_params.temperature <= 0.0f) {
    llama_sampler_chain_add(m_chain, llama_sampler_init_greedy());
  } else {
    llama_sampler_chain_add(m_chain, llama_sampler_init_dist(m_params.seed));
  }
}

void InferenceEngine::freeSampler() {
  if (m_chain) {
    llama_sampler_free(m_chain);
    m_chain = nullptr;
  }
}

InferenceEngine::~InferenceEngine() { freeSampler(); }

// ── Text-only inference ───────────────────────────────────────────

/// Tokenize the prompt, decode it into the KV cache (in n_batch-sized
/// chunks), then hand off to the shared generate() loop.
///
/// The prompt is split into batches because llama_decode has an internal
/// buffer whose size scales with n_batch.  If the prompt exceeds n_batch
/// tokens, we process it in chunks to avoid allocating excessively large
/// temporary buffers.
QString InferenceEngine::inferText(const QString &prompt) {
  // Clear stale KV cache from any previous inference on this context.
  clearKvCache();

  QByteArray data = prompt.toUtf8();
  const char *text = data.constData();
  int32_t textLen = static_cast<int32_t>(data.size());

  // Tokenize
  constexpr int32_t MAX_TOKS = 16384;
  std::vector<llama_token> tokens(MAX_TOKS);
  int32_t nTokens = llama_tokenize(m_vocab, text, textLen, tokens.data(),
                                   MAX_TOKS, true, true);
  if (nTokens <= 0) {
    qWarning() << "[InferenceEngine] Tokenization failed (n_tokens=" << nTokens
               << ")";
    return QString();
  }
  tokens.resize(static_cast<size_t>(nTokens));

  qDebug() << "[InferenceEngine] Prompt tokens:" << nTokens;

  // Decode the prompt in chunks that fit within n_batch.
  // Each chunk advances the position counter so the KV cache keeps track
  // of absolute positions (required for RoPE embeddings).
  // We only request logits at the very last token of the final chunk —
  // all intermediate positions are just being cached, not sampled.
  for (int32_t i = 0; i < nTokens; i += m_n_batch) {
    int32_t nCurr = std::min(m_n_batch, nTokens - i);
    bool lastChunk = (i + nCurr >= nTokens);

    llama_batch batch = llama_batch_init(nCurr, 0, 1);
    batch.n_tokens = nCurr;

    for (int32_t j = 0; j < nCurr; ++j) {
      batch.token[j] = tokens[i + j];
      batch.pos[j] = i + j;
      batch.seq_id[j][0] = 0;
      batch.n_seq_id[j] = 1;
      batch.logits[j] = false;
    }
    // Need logits at the last position of the final chunk.
    if (lastChunk)
      batch.logits[nCurr - 1] = true;

    int32_t res = llama_decode(m_ctx, batch);
    llama_batch_free(batch);

    if (res != 0) {
      qCritical() << "[InferenceEngine] Prompt decode failed at offset" << i
                  << "with error:" << res;
      return QString();
    }
  }

  return generate();
}

// ── Multimodal inference ──────────────────────────────────────────

/// Tokenize the prompt + images together using mtmd, which produces
/// interleaved text and image chunks.  Each image is encoded through the
/// vision encoder (Siglip) and its embeddings are decoded at the position
/// of the media marker in the prompt.
///
/// The key difference from text-only inference:
///   - mtmd_tokenize replaces media markers with image token slots
///   - mtmd_helper_eval_chunks handles the mixed text/image decode loop
///     (text → normal decode, images → vision encode + embedding decode)
///   - M-RoPE positions are tracked by mtmd_helper_eval_chunks
QString InferenceEngine::inferMultimodal(const QString &prompt,
                                         const std::vector<PageImage> &images,
                                         struct mtmd_context *mctx) {

  // Clear stale KV cache from any previous inference on this context.
  clearKvCache();

  if (!mctx || !mtmd_support_vision(mctx)) {
    qWarning() << "[InferenceEngine] Multimodal requested but vision "
                  "not available";
    return QString();
  }

  // Convert PageImage → mtmd_bitmap (wraps the raw pixel buffer, no copy).
  std::vector<const mtmd_bitmap *> bitmaps;
  bitmaps.reserve(images.size());

  for (const auto &img : images) {
    auto *bmp = mtmd_bitmap_init(img.width, img.height, img.data.data());
    if (bmp) {
      bitmaps.push_back(bmp);
    } else {
      qWarning() << "[InferenceEngine] Failed to create bitmap" << img.width
                 << "x" << img.height;
    }
  }

  if (bitmaps.empty()) {
    qWarning() << "[InferenceEngine] No valid bitmaps created";
    for (auto *bmp : bitmaps) {
      mtmd_bitmap_free(const_cast<mtmd_bitmap *>(bmp));
    }
    return QString();
  }

  QByteArray data = prompt.toUtf8();
  mtmd_input_text textInput{data.constData(), true, false};

  // Tokenize with mtmd (produces interleaved text + image chunks)
  mtmd_input_chunks *chunks = mtmd_input_chunks_init();
  int32_t result = mtmd_tokenize(mctx, chunks, &textInput, bitmaps.data(),
                                 static_cast<size_t>(bitmaps.size()));

  if (result != 0) {
    qCritical() << "[InferenceEngine] mtmd_tokenize failed with:" << result;
    mtmd_input_chunks_free(chunks);
    for (auto *bmp : bitmaps) {
      mtmd_bitmap_free(const_cast<mtmd_bitmap *>(bmp));
    }
    return QString();
  }

  size_t chunkCount = mtmd_input_chunks_size(chunks);
  size_t totalTokens = mtmd_helper_get_n_tokens(chunks);
  qDebug() << "[InferenceEngine] mtmd chunks:" << chunkCount
           << "| total tokens:" << totalTokens;

  // mtmd_helper_eval_chunks handles the entire eval loop:
  //   - text chunks → llama_decode()
  //   - image chunks → mtmd_encode_chunk() + embedding decode
  //   - M-RoPE position tracking
  //   - batching respects n_ubatch
  llama_pos nPast = 0;
  int32_t res =
      mtmd_helper_eval_chunks(mctx, m_ctx, chunks, nPast, 0,
                              llama_n_batch(m_ctx), // n_batch from context
                              true, // logits_last: we need logits at the end
                              &nPast);

  mtmd_input_chunks_free(chunks);

  // Clean up bitmaps
  for (auto *bmp : bitmaps) {
    mtmd_bitmap_free(const_cast<mtmd_bitmap *>(bmp));
  }

  if (res != 0) {
    qCritical() << "[InferenceEngine] mtmd_helper_eval_chunks failed:" << res;
    return QString();
  }

  qDebug() << "[InferenceEngine] Multimodal prompt decoded, n_past=" << nPast;

  return generate();
}

// ── Shared generation loop ────────────────────────────────────────

/// Autoregressive token generation.  The prompt has already been decoded
/// into the KV cache; we sample tokens one at a time until EOS, a stop
/// sequence, or the token budget is exhausted.
///
/// Position tracking: instead of passing n_past explicitly, we query the
/// KV cache for the highest occupied position via llama_memory_seq_pos_max.
/// This works uniformly for both text-only and multimodal paths because
/// mtmd_helper_eval_chunks already wrote its position into the cache.
QString InferenceEngine::generate() {
  QString output;

  bool eosFound = false;

  const auto eosToken = llama_vocab_eos(m_vocab);
  const auto eotToken = llama_vocab_eot(m_vocab);

  // Query the KV cache to find where the prompt left off.
  // We use seq_pos_max on sequence 0 (the only sequence we use) to get
  // the last position written.  Adding 1 gives us the next free slot.
  // This is more robust than tracking nPast manually because it works
  // correctly even when the prompt path changes (text vs multimodal).
  llama_pos nPast = 0;
  {
    auto mem = llama_get_memory(m_ctx);
    llama_pos maxPos = llama_memory_seq_pos_max(mem, 0);
    if (maxPos >= 0) {
      nPast = maxPos + 1; // next free position
    }
  }

  int32_t tokenCount = 0;

  for (int32_t i = 0; i < m_params.max_tokens; ++i) {
    // Sample next token from logits at the last position (-1 = last).
    // The sampler chain applies all configured transformations (temp,
    // top-k, top-p, etc.) before picking the token.
    llama_token tokenId = llama_sampler_sample(m_chain, m_ctx, -1);

    // Accept into sampler state so penalty trackers know what was emitted.
    llama_sampler_accept(m_chain, tokenId);

    // Check for EOS / EOT
    if (tokenId == eosToken || tokenId == eotToken) {
      eosFound = true;
      break;
    }

    // Convert to text and accumulate
    QString piece = tokenToPiece(tokenId);
    output += piece;
    emit tokenGenerated(piece);

    // Check stop sequence
    if (!m_params.stop_sequence.isEmpty() && shouldStop(output)) {
      break;
    }

    // Decode the single token to get logits for the next sample
    llama_batch batch = llama_batch_init(1, 0, 1);
    batch.token[0] = tokenId;
    batch.pos[0] = nPast + i;
    batch.seq_id[0][0] = 0;
    batch.n_seq_id[0] = 1;
    batch.logits[0] = true;
    batch.n_tokens = 1;

    tokenCount++;

    int32_t res = llama_decode(m_ctx, batch);
    llama_batch_free(batch);

    if (res != 0) {
      qCritical() << "[InferenceEngine] Decode failed at token" << i
                  << "with error:" << res;
      break;
    }
  }

  if (!eosFound) {
    qDebug() << "[InferenceEngine] Generation stopped after"
             << m_params.max_tokens << "tokens (no EOS)";
  }

  return output;
}

// ── KV cache management ──────────────────────────────────────────

void InferenceEngine::clearKvCache() {
  auto mem = llama_get_memory(m_ctx);
  llama_memory_clear(mem, true);
}

// ── Helpers ───────────────────────────────────────────────────────

bool InferenceEngine::shouldStop(const QString &output) const {
  if (m_params.stop_sequence.isEmpty()) {
    return false;
  }
  return output.endsWith(m_params.stop_sequence);
}

/// Convert a single token id back to its UTF-8 string representation.
/// Uses a reusable buffer (m_pieceBuf) to avoid per-token allocations.
/// The lstrip_space flag strips the leading-space prefix that BPE tokenizers
/// add to continuation tokens — this keeps output readable.
QString InferenceEngine::tokenToPiece(llama_token tok) const {
  m_pieceBuf.resize(32);
  int32_t n = llama_token_to_piece(m_vocab, tok, m_pieceBuf.data(),
                                   static_cast<int32_t>(m_pieceBuf.size()),
                                   false, true);

  if (n > 0) {
    return QString::fromUtf8(m_pieceBuf.data(), n);
  }

  // Buffer too small — resize and retry
  m_pieceBuf.resize(256);
  n = llama_token_to_piece(m_vocab, tok, m_pieceBuf.data(),
                           static_cast<int32_t>(m_pieceBuf.size()), false,
                           true);
  if (n > 0) {
    return QString::fromUtf8(m_pieceBuf.data(), n);
  }

  // Fallback
  return QString::number(tok);
}
