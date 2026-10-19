#include "inferenceengine.h"

#include "common/common.h"
#include "common/sampling.h"
#include "llama-cpp.h"
#include "llama.h"
#include "localbackend.h"
#include "mtmd-helper.h"
#include "mtmd.h"

#include <QCoreApplication>
#include <QDebug>
#include <algorithm>
#include <array>
#include <memory>
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

// ── KV cache management ────────────────────────────────────────────

void InferenceEngine::clearKvCache() {
  auto mem = llama_get_memory(m_ctx);
  llama_memory_clear(mem, true);
}

/// Current max position + 1 across all sequences (== number of positions
/// already consumed).  Used to resume generation after a prompt/multimodal
/// pass.  Returns 0 if the memory is empty.
int32_t InferenceEngine::currentNpast() const {
  auto mem = llama_get_memory(m_ctx);
  llama_pos maxPos = llama_memory_seq_pos_max(mem, 0);
  return maxPos >= 0 ? (int32_t)(maxPos + 1) : 0;
}

// ── Text-only inference ───────────────────────────────────────────

QString InferenceEngine::inferText(const QString &prompt) {
  qDebug() << "[InferenceEngine] DIAG inferText START | prompt chars:"
           << prompt.length();
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

  // Decode the prompt in n_batch-sized chunks via the extended batch API
  // (llama_batch_ext + llama_process), which replaced llama_batch/llama_decode
  // in llama.cpp 0.6.0.  Output logits are requested only on the very last
  // token of the whole prompt (the sampler reads the final position).
  common_batch batch(m_ctx);
  llama_pos pos = 0;
  while (pos < (llama_pos)tokens.size()) {
    llama_pos end =
        std::min(pos + (llama_pos)m_n_batch, (llama_pos)tokens.size());
    for (llama_pos i = pos; i < end; ++i) {
      const bool isLast = (i == (llama_pos)tokens.size() - 1);
      if (batch.add(tokens[i], i, 0, isLast) < 0) {
        qCritical() << "[InferenceEngine] Failed to add prompt token at" << i;
        return QString();
      }
    }

    if (llama_process(m_ctx, LLAMA_PROCESS_TYPE_DECODE, batch.get()) != 0) {
      qCritical() << "[InferenceEngine] Prompt decode failed at offset" << pos;
      return QString();
    }
    batch.clear();
    pos = end;
  }

  qDebug()
      << "[InferenceEngine] DIAG inferText: prompt decoded, calling generate()";
  return generate();
}

// ── Multimodal inference ──────────────────────────────────────────
// Migrated to the llama.cpp 0.6.0 multimodal API (see tools/mtmd/mtmd-cli.cpp):
//   - build chunks with mtmd_tokenize
//   - text chunks  -> mtmd_helper_eval_chunk_single
//   - media chunks -> mtmd_batch_encode + mtmd_helper_decode_image_chunk
// The deprecated mtmd_helper_eval_chunks / llama_decode path was removed.

QString InferenceEngine::inferMultimodal(const QString &prompt,
                                         const std::vector<PageImage> &images,
                                         struct mtmd_context *mctx) {
  qDebug() << "[InferenceEngine] DIAG inferMultimodal START | images:"
           << images.size() << "| mctx:" << (mctx ? "valid" : "NULL");
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
    for (auto *bmp : bitmaps)
      mtmd_bitmap_free(const_cast<mtmd_bitmap *>(bmp));
    return QString();
  }

  auto cleanup = [&](int * /*out*/ = nullptr) {
    for (auto *bmp : bitmaps)
      mtmd_bitmap_free(const_cast<mtmd_bitmap *>(bmp));
  };

  QByteArray data = prompt.toUtf8();
  mtmd_input_text textInput{data.constData(), static_cast<size_t>(data.size()),
                            true, false};

  mtmd::input_chunks_ptr chunks(mtmd_input_chunks_init());
  int32_t result = mtmd_tokenize(mctx, chunks.get(), &textInput, bitmaps.data(),
                                 static_cast<size_t>(bitmaps.size()));
  if (result != 0) {
    qCritical() << "[InferenceEngine] mtmd_tokenize failed with:" << result;
    cleanup();
    return QString();
  }

  const size_t nChunks = mtmd_input_chunks_size(chunks.get());
  qDebug() << "[InferenceEngine] mtmd chunks:" << nChunks;

  llama_pos nPast = 0;
  mtmd::batch_ptr mbatch; // lazily created for runs of consecutive media chunks

  for (size_t i = 0; i < nChunks; ++i) {
    auto chunk = mtmd_input_chunks_get(chunks.get(), i);
    auto chunkType = mtmd_input_chunk_get_type(chunk);

    if (chunkType == MTMD_INPUT_CHUNK_TYPE_TEXT) {
      // A text chunk terminates the current media batch.
      if (mbatch) {
        mbatch.reset();
      }
      llama_pos newNpast = nPast;
      int32_t res = mtmd_helper_eval_chunk_single(
          mctx, m_ctx, chunk, nPast, 0, m_n_batch, i == nChunks - 1, &newNpast);
      if (res != 0) {
        qCritical() << "[InferenceEngine] eval text chunk" << i
                    << "failed:" << res;
        cleanup();
        return QString();
      }
      nPast = newNpast;
    } else {
      // Media chunk: batch-encode it, then decode the resulting embeddings.
      float *embd =
          mbatch ? mtmd_batch_get_output_embd(mbatch.get(), chunk) : nullptr;
      if (!embd) {
        // Start (or restart) a media batch and pack in as many consecutive
        // media chunks as possible before the next text chunk.
        mbatch.reset(mtmd_batch_init(mctx));
        if (mtmd_batch_add_chunk(mbatch.get(), chunk) != 0) {
          qCritical() << "[InferenceEngine] mtmd_batch_add_chunk failed for"
                      << i;
          cleanup();
          return QString();
        }
        for (size_t j = i + 1; j < nChunks; ++j) {
          auto next = mtmd_input_chunks_get(chunks.get(), j);
          if (mtmd_input_chunk_get_type(next) == MTMD_INPUT_CHUNK_TYPE_TEXT)
            break;
          if (mtmd_batch_add_chunk(mbatch.get(), next) != 0)
            break;
        }
        int32_t res = mtmd_batch_encode(mbatch.get());
        if (res != 0) {
          qCritical() << "[InferenceEngine] mtmd_batch_encode failed:" << res;
          cleanup();
          return QString();
        }
        embd = mtmd_batch_get_output_embd(mbatch.get(), chunk);
      }
      if (!embd) {
        qCritical() << "[InferenceEngine] no embeddings for media chunk" << i;
        cleanup();
        return QString();
      }

      llama_pos newNpast = nPast;
      int32_t res = mtmd_helper_decode_image_chunk(mctx, m_ctx, chunk, embd,
                                                   nPast, 0, m_n_batch,
                                                   &newNpast, nullptr, nullptr);
      if (res != 0) {
        qCritical() << "[InferenceEngine] decode image chunk" << i
                    << "failed:" << res;
        cleanup();
        return QString();
      }
      nPast = newNpast;
    }
  }

  qDebug() << "[InferenceEngine] Multimodal prompt decoded, n_past=" << nPast;
  cleanup();
  qDebug() << "[InferenceEngine] DIAG inferMultimodal: calling generate()";
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
  llama_pos nPast = currentNpast();
  qDebug() << "[InferenceEngine] DIAG generate START | n_past:" << nPast;

  // Poll for a queued stop() every kPollTokens generated tokens. The
  // generation loop is synchronous and blocks the worker's event loop, so a
  // queued stop() slot cannot run until we pump events; this is what makes
  // cooperative cancellation actually interrupt a long local generation.
  const int kPollTokens = 32;

  while (true) {
    if (m_cancelFlag && *m_cancelFlag) {
      qInfo() << "[InferenceEngine] Generation cancelled after" << i
              << "tokens";
      break;
    }

    llama_token tokenId = common_sampler_sample(m_sampler.get(), m_ctx, -1);
    common_sampler_accept(m_sampler.get(), tokenId, true);

    if (llama_vocab_is_eog(m_vocab, tokenId)) {
      qDebug() << "[InferenceEngine] Generation stopped on EOS after" << i
               << "tokens | final position:" << (nPast + i)
               << "/ n_ctx:" << llama_n_ctx(m_ctx);
      break;
    }

    QString piece = tokenToPiece(tokenId);
    output += piece;
    emit tokenGenerated(piece);

    if (!m_stopSequence.isEmpty() && output.endsWith(m_stopSequence)) {
      break;
    }

    // Decode the single token via llama_batch_ext + llama_process
    // (replaces the removed llama_decode path).
    {
      common_batch batch(m_ctx);
      if (batch.add(tokenId, nPast + i, 0, /*output=*/true) >= 0) {
        if (llama_process(m_ctx, LLAMA_PROCESS_TYPE_DECODE, batch.get()) != 0) {
          qCritical() << "[InferenceEngine] Decode failed at token" << i
                      << "| position:" << (nPast + i)
                      << "/ n_ctx:" << llama_n_ctx(m_ctx)
                      << "— KV cache likely exhausted, output truncated";
          break;
        }
      }
    }
    ++i;

    if (m_cancelFlag && (i % kPollTokens == 0))
      QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 0);
  }

  return output;
}

// ── Helpers ────────────────────────────────────────────────────────

/// common_token_to_piece handles all buffer management internally.
QString InferenceEngine::tokenToPiece(llama_token tok) const {
  std::string piece = common_token_to_piece(m_ctx, tok, true);
  return QString::fromStdString(piece);
}
