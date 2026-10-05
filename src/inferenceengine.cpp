#include "inferenceengine.h"

#include "common/common.h"
#include "common/sampling.h"
#include "llama.h"
#include "localbackend.h"
#include "mtmd-helper.h"
#include "mtmd.h"

#include <QCoreApplication>
#include <QDebug>
#include <algorithm>
#include <vector>

InferenceEngine::InferenceEngine(struct llama_context *ctx,
                                 struct llama_model *model,
                                 const common_params_sampling &samplingParams,
                                 QObject *parent)
    : QObject(parent), m_ctx(ctx), m_model(model),
      m_vocab(llama_model_get_vocab(model)), m_n_batch(llama_n_batch(ctx)),
      m_sampler(common_sampler_init(model, const_cast<common_params_sampling &>(
                                               samplingParams)),
                &common_sampler_free) {
  if (m_sampler) {
    qDebug() << "[InferenceEngine] Sampler chain:"
             << QString::fromStdString(common_sampler_print(m_sampler.get()));
  } else {
    qCritical() << "[InferenceEngine] Failed to initialize sampler";
  }
}

InferenceEngine::~InferenceEngine() = default;

// ── Text-only inference ───────────────────────────────────────────

QString InferenceEngine::inferText(const QString &prompt) {
  clearKvCache();

  // common_tokenize returns a std::vector — no manual buffer sizing.
  QByteArray data = prompt.toUtf8();
  auto tokens =
      common_tokenize(m_vocab, std::string(data.constData()), true, true);
  if (tokens.empty()) {
    qWarning() << "[InferenceEngine] Tokenization returned empty";
    return QString();
  }

  qDebug() << "[InferenceEngine] Prompt tokens:" << tokens.size();

  // Decode the prompt in n_batch-sized chunks. The last token of each chunk
  // is the one we request logits for (the final token of the prompt).
  llama_pos pos = 0;
  while (pos < (llama_pos)tokens.size()) {
    llama_pos end =
        std::min(pos + (llama_pos)m_n_batch, (llama_pos)tokens.size());

    // Manual batch: llama_batch_get_one always sets output on the last
    // token of the whole sequence, but during a chunked prompt pass we want
    // output only on the very last token of the *whole* prompt.
    struct llama_batch batch = llama_batch_init(end - pos, 0, 1);
    for (llama_pos i = pos; i < end; ++i) {
      batch.n_tokens       = i - pos + 1;
      batch.token[i - pos] = tokens[i];
      batch.pos[i - pos]   = i;
      batch.n_seq_id[i - pos] = 1;
      batch.seq_id[i - pos][0] = 0;
      batch.logits[i - pos] =
          (i == (llama_pos)tokens.size() - 1) ? 1 : 0;
    }

    int32_t res = llama_decode(m_ctx, batch);
    llama_batch_free(batch);

    if (res != 0) {
      qCritical() << "[InferenceEngine] Prompt decode failed at offset" << pos
                  << "with error:" << res;
      return QString();
    }
    pos = end;
  }

  return generate();
}

// ── Multimodal inference ──────────────────────────────────────────

QString InferenceEngine::inferMultimodal(const QString &prompt,
                                         const std::vector<PageImage> &images,
                                         struct mtmd_context *mctx) {
  clearKvCache();

  if (!mctx || !mtmd_support_vision(mctx)) {
    qWarning() << "[InferenceEngine] Multimodal requested but vision "
                  "not available";
    return QString();
  }

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
  mtmd_input_text textInput{data.constData(),
                           static_cast<size_t>(data.size()), true, false};

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

  llama_pos nPast = 0;
  int32_t res = mtmd_helper_eval_chunks(mctx, m_ctx, chunks, nPast, 0,
                                        llama_n_batch(m_ctx), true, &nPast);

  mtmd_input_chunks_free(chunks);

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

QString InferenceEngine::generate() {
  if (!m_sampler) {
    qCritical() << "[InferenceEngine] No sampler available";
    return QString();
  }

  QString output;
  int32_t i = 0;

  llama_pos nPast = 0;
  {
    auto mem = llama_get_memory(m_ctx);
    llama_pos maxPos = llama_memory_seq_pos_max(mem, 0);
    if (maxPos >= 0) {
      nPast = maxPos + 1;
    }
  }

  // Poll for a queued stop() every kPollTokens generated tokens. The
  // generation loop is synchronous and blocks the worker's event loop, so a
  // queued stop() slot cannot run until we pump events; this is what makes
  // cooperative cancellation actually interrupt a long local generation.
  const int kPollTokens = 32;

  while (true) {
    // Cooperative cancellation: if the caller set the flag (via stop()),
    // stop at this token boundary and return the partial output.
    if (m_cancelFlag && *m_cancelFlag) {
      qInfo() << "[InferenceEngine] Generation cancelled after" << i
              << "tokens";
      break;
    }

    llama_token tokenId = common_sampler_sample(m_sampler.get(), m_ctx, -1);
    common_sampler_accept(m_sampler.get(), tokenId, true);

    // llama_vocab_is_eog covers eos, eot, and model-specific end tokens.
    if (llama_vocab_is_eog(m_vocab, tokenId)) {
      break;
    }

    QString piece = tokenToPiece(tokenId);
    output += piece;
    emit tokenGenerated(piece);

    if (!m_stopSequence.isEmpty() && output.endsWith(m_stopSequence)) {
      break;
    }

    // Decode the single token.
    {
      struct llama_batch batch = llama_batch_init(1, 0, 1);
      batch.n_tokens = 1;
      batch.token[0] = tokenId;
      batch.pos[0]   = nPast + i;
      batch.n_seq_id[0] = 1;
      batch.seq_id[0][0] = 0;
      batch.logits[0] = 1;

      int32_t res = llama_decode(m_ctx, batch);
      llama_batch_free(batch);

      if (res != 0) {
        qCritical() << "[InferenceEngine] Decode failed at token" << i
                    << "with error:" << res;
        break;
      }
    }
    ++i;

    // Pump the event loop periodically so a queued stop() can run and set
    // m_cancelFlag (checked at the top of the next iteration).
    if (m_cancelFlag && (i % kPollTokens == 0))
      QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 0);
  }

  return output;
}

// ── KV cache management ──────────────────────────────────────────

void InferenceEngine::clearKvCache() {
  auto mem = llama_get_memory(m_ctx);
  llama_memory_clear(mem, true);
}

// ── Helpers ───────────────────────────────────────────────────────

/// common_token_to_piece handles all buffer management internally.
QString InferenceEngine::tokenToPiece(llama_token tok) const {
  std::string piece = common_token_to_piece(m_ctx, tok, true);
  return QString::fromStdString(piece);
}
