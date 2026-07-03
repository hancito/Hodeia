#include "headers/llama_inference_engine.h"

#include "llama.h"
#include "mtmd-helper.h"
#include "mtmd.h"

#include <QDebug>
#include <QThread>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace std::chrono_literals;

LlamaInferenceEngine::LlamaInferenceEngine(struct llama_context *ctx,
                                           struct llama_model *model,
                                           const InferenceParams &params,
                                           QObject *parent)
    : QObject(parent), m_ctx(ctx), m_model(model),
      m_vocab(llama_model_get_vocab(model)), m_params(params) {
  if (m_params.n_threads == 0) {
    m_params.n_threads = QThread::idealThreadCount();
  }
  m_pieceBuf.resize(32);
  buildSampler();
}

// ── Sampler chain construction ────────────────────────────────────

void LlamaInferenceEngine::buildSampler() {
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

void LlamaInferenceEngine::freeSampler() {
  if (m_chain) {
    llama_sampler_free(m_chain);
    m_chain = nullptr;
  }
}

LlamaInferenceEngine::~LlamaInferenceEngine() { freeSampler(); }

// ── Text-only inference ───────────────────────────────────────────

QString LlamaInferenceEngine::inferText(const QString &prompt) {
  QByteArray data = prompt.toUtf8();
  const char *text = data.constData();
  int32_t textLen = static_cast<int32_t>(data.size());

  // Tokenize
  constexpr int32_t MAX_TOKS = 16384;
  std::vector<llama_token> tokens(MAX_TOKS);
  int32_t nTokens = llama_tokenize(m_vocab, text, textLen, tokens.data(),
                                   MAX_TOKS, true, true);
  if (nTokens <= 0) {
    qWarning() << "[LlamaInferenceEngine] Tokenization failed (n_tokens="
               << nTokens << ")";
    return QString();
  }
  tokens.resize(static_cast<size_t>(nTokens));

  qDebug() << "[LlamaInferenceEngine] Prompt tokens:" << nTokens;

  // Decode the prompt in one batch
  llama_batch batch = llama_batch_init(nTokens, 0, 1);
  batch.n_tokens = nTokens;

  for (int32_t i = 0; i < nTokens; ++i) {
    batch.token[i] = tokens[i];
    batch.pos[i] = i;
    batch.seq_id[i][0] = 0;
    batch.n_seq_id[i] = 1;
    batch.logits[i] = false; // we only need logits for the last position
  }
  // We need logits at the last position to start sampling
  batch.logits[nTokens - 1] = true;

  int32_t res = llama_decode(m_ctx, batch);
  llama_batch_free(batch);

  if (res != 0) {
    qCritical() << "[LlamaInferenceEngine] Prompt decode failed with error:"
                << res;
    return QString();
  }

  return generate();
}

// ── Multimodal inference ──────────────────────────────────────────

QString
LlamaInferenceEngine::inferMultimodal(const QString &prompt,
                                      const std::vector<RawImage> &images,
                                      struct mtmd_context *mctx) {

  if (!mctx || !mtmd_support_vision(mctx)) {
    qWarning() << "[LlamaInferenceEngine] Multimodal requested but vision "
                  "not available";
    return QString();
  }

  // Convert RawImage → mtmd_bitmap internally
  std::vector<const mtmd_bitmap *> bitmaps;
  bitmaps.reserve(images.size());

  for (const auto &img : images) {
    auto *bmp = mtmd_bitmap_init(img.width, img.height, img.data.data());
    if (bmp) {
      bitmaps.push_back(bmp);
    } else {
      qWarning() << "[LlamaInferenceEngine] Failed to create bitmap"
                 << img.width << "x" << img.height;
    }
  }

  if (bitmaps.empty()) {
    qWarning() << "[LlamaInferenceEngine] No valid bitmaps created";
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
    qCritical() << "[LlamaInferenceEngine] mtmd_tokenize failed with:"
                << result;
    mtmd_input_chunks_free(chunks);
    for (auto *bmp : bitmaps) {
      mtmd_bitmap_free(const_cast<mtmd_bitmap *>(bmp));
    }
    return QString();
  }

  size_t chunkCount = mtmd_input_chunks_size(chunks);
  size_t totalTokens = mtmd_helper_get_n_tokens(chunks);
  qDebug() << "[LlamaInferenceEngine] mtmd chunks:" << chunkCount
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
    qCritical() << "[LlamaInferenceEngine] mtmd_helper_eval_chunks failed:"
                << res;
    return QString();
  }

  qDebug() << "[LlamaInferenceEngine] Multimodal prompt decoded, n_past="
           << nPast;

  return generate();
}

// ── Shared generation loop ────────────────────────────────────────

QString LlamaInferenceEngine::generate() {
  QString output;

  bool eosFound = false;

  const auto eosToken = llama_vocab_eos(m_vocab);
  const auto eotToken = llama_vocab_eot(m_vocab);

  // Query the KV cache to find where the prompt left off.
  // This works for both text-only (pos_max == n_prompt_tokens-1)
  // and multimodal (pos_max == n_past from mtmd_helper_eval_chunks).
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
    // Sample next token from logits at the last position
    llama_token tokenId = llama_sampler_sample(m_chain, m_ctx, -1);

    // Accept into sampler state (penalties, grammar, etc.)
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
      qCritical() << "[LlamaInferenceEngine] Decode failed at token" << i
                  << "with error:" << res;
      break;
    }
  }

  if (!eosFound) {
    qDebug() << "[LlamaInferenceEngine] Generation stopped after"
             << m_params.max_tokens << "tokens (no EOS)";
  }

  return output;
}

// ── Helpers ───────────────────────────────────────────────────────

bool LlamaInferenceEngine::shouldStop(const QString &output) const {
  if (m_params.stop_sequence.isEmpty()) {
    return false;
  }
  return output.endsWith(m_params.stop_sequence);
}

QString LlamaInferenceEngine::tokenToPiece(llama_token tok) const {
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
