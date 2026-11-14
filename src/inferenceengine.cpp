#include "inferenceengine.h"

#include "common/chat.h"
#include "common/common.h"
#include "common/sampling.h"
#include "llama.h"
#include "localbackend.h"
#include "mtmd-helper.h"
#include "mtmd.h"

#include <QCoreApplication>
#include <QDebug>
#include <algorithm>
#include <memory>
#include <vector>

namespace {

/// Length of the longest prefix of `s` that does not end in the middle of a
/// UTF-8 multi-byte sequence. Invalid bytes are left for QString::fromUtf8
/// to replace rather than being held back.
size_t utf8CompletePrefixLen(const std::string &s) {
  const size_t n = s.size();
  for (size_t back = 1; back <= 4 && back <= n; ++back) {
    const auto c = static_cast<unsigned char>(s[n - back]);
    if ((c & 0xC0) == 0x80)
      continue; // continuation byte — keep looking for the lead byte
    size_t need = 1;
    if ((c & 0xE0) == 0xC0)
      need = 2;
    else if ((c & 0xF0) == 0xE0)
      need = 3;
    else if ((c & 0xF8) == 0xF0)
      need = 4;
    return back < need ? n - back : n;
  }
  return n;
}

/// One piece of a multimodal prompt: either text (with its own
/// parse_special) or the index of an image.
struct PromptSegment {
  std::string text;
  bool parseSpecial = true;
  int image = -1; // >= 0: index into the images vector
};

/// Split a rendered prompt into segments for mtmd_tokenize_from_parts:
/// template parts keep special-token parsing, user-input parts lose it and
/// are cut at each media marker into text + image segments.  Returns the
/// number of markers found.
size_t splitPrompt(const common_chat_prompt &prompt, const std::string &marker,
                   std::vector<PromptSegment> &out) {
  size_t nMarkers = 0;
  for (const auto &part : prompt.parts) {
    if (!part.is_input) {
      out.push_back({part.text, true, -1});
      continue;
    }
    size_t start = 0;
    while (true) {
      const size_t at = part.text.find(marker, start);
      const size_t end = at == std::string::npos ? part.text.size() : at;
      if (end > start)
        out.push_back({part.text.substr(start, end - start), false, -1});
      if (at == std::string::npos)
        break;
      out.push_back({{}, false, static_cast<int>(nMarkers++)});
      start = at + marker.size();
    }
  }
  return nMarkers;
}

/// Tokenize a multimodal prompt into `chunks`.  Bitmaps are validated
/// (tightly packed RGB) and must match the markers one for one.
/// Returns false (and logs) on any error.
bool tokenizeMultimodal(mtmd_context *mctx, const common_chat_prompt &prompt,
                        const std::vector<PageImage> &images,
                        mtmd_input_chunks *chunks) {
  mtmd::bitmaps bitmaps;
  bitmaps.entries.reserve(images.size());
  for (const auto &img : images) {
    const size_t expected =
        (img.width > 0 && img.height > 0)
            ? static_cast<size_t>(img.width) * static_cast<size_t>(img.height) * 3
            : 0;
    if (expected == 0 || img.data.size() != expected) {
      qCritical() << "[InferenceEngine] Invalid image" << img.width << "x"
                  << img.height << "| bytes:" << img.data.size()
                  << "expected:" << expected;
      return false;
    }
    mtmd::bitmap bmp(static_cast<uint32_t>(img.width),
                     static_cast<uint32_t>(img.height), img.data.data());
    if (!bmp.ptr) {
      qCritical() << "[InferenceEngine] Failed to create bitmap" << img.width
                  << "x" << img.height;
      return false;
    }
    bitmaps.entries.push_back(std::move(bmp));
  }

  std::vector<PromptSegment> segments;
  const size_t nMarkers =
      splitPrompt(prompt, mtmd_get_marker(mctx), segments);
  if (nMarkers != images.size()) {
    qCritical() << "[InferenceEngine] Prompt has" << nMarkers
                << "media markers but" << images.size() << "images";
    return false;
  }

  // mtmd_input_part points into these vectors: size them up front so no
  // reallocation invalidates the pointers.
  std::vector<mtmd_input_text> texts;
  texts.reserve(segments.size());
  std::vector<mtmd_input_part> parts;
  parts.reserve(segments.size());
  for (const auto &seg : segments) {
    if (seg.image >= 0) {
      parts.push_back({nullptr, bitmaps.entries[seg.image].ptr.get()});
    } else {
      texts.push_back({seg.text.c_str(), seg.text.size(),
                       /*add_special=*/false, seg.parseSpecial});
      parts.push_back({&texts.back(), nullptr});
    }
  }
  std::vector<const mtmd_input_part *> partPtrs;
  partPtrs.reserve(parts.size());
  for (const auto &p : parts)
    partPtrs.push_back(&p);

  const int32_t res = mtmd_tokenize_from_parts(
      mctx, chunks, partPtrs.data(), partPtrs.size(), /*add_special=*/true);
  if (res != 0) {
    qCritical() << "[InferenceEngine] mtmd_tokenize_from_parts failed:" << res;
    return false;
  }
  return true;
}

} // namespace

std::vector<llama_token>
InferenceEngine::tokenizePrompt(const llama_vocab *vocab,
                                const common_chat_prompt &prompt) {
  // Only the first part may add BOS; user-input parts never parse special
  // tokens.  Tokenizing per part can split a BPE merge at a part boundary,
  // which is harmless (template/content boundaries are whitespace or
  // special tokens).
  std::vector<llama_token> tokens;
  bool first = true;
  for (const auto &part : prompt.parts) {
    auto t = common_tokenize(vocab, part.text, /*add_special=*/first,
                             /*parse_special=*/!part.is_input);
    tokens.insert(tokens.end(), t.begin(), t.end());
    first = false;
  }
  return tokens;
}

int32_t
InferenceEngine::countMultimodalTokens(mtmd_context *mctx,
                                       const common_chat_prompt &prompt,
                                       const std::vector<PageImage> &images) {
  if (!mctx)
    return -1;
  mtmd::input_chunks_ptr chunks(mtmd_input_chunks_init());
  if (!tokenizeMultimodal(mctx, prompt, images, chunks.get()))
    return -1;
  return static_cast<int32_t>(mtmd_helper_get_n_tokens(chunks.get()));
}

// samplingParams is taken by value: common_sampler_init may mutate it.
InferenceEngine::InferenceEngine(struct llama_context *ctx,
                                 struct llama_model *model,
                                 common_params_sampling samplingParams,
                                 QObject *parent)
    : QObject(parent), m_ctx(ctx), m_model(model),
      m_vocab(llama_model_get_vocab(model)), m_n_batch(llama_n_batch(ctx)),
      m_sampler(common_sampler_init(model, samplingParams),
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

// ── Text-only inference ───────────────────────────────────────────

QString InferenceEngine::inferText(const common_chat_prompt &prompt) {
  clearKvCache();

  auto tokens = tokenizePrompt(m_vocab, prompt);
  if (tokens.empty()) {
    qWarning() << "[InferenceEngine] Tokenization returned empty";
    return QString();
  }

  const auto nTokens = (llama_pos)tokens.size();
  const auto nCtx = (llama_pos)llama_n_ctx(m_ctx);
  qDebug() << "[InferenceEngine] Prompt tokens:" << nTokens << "/ n_ctx:"
           << nCtx;
  if (nTokens >= nCtx) {
    qCritical() << "[InferenceEngine] Prompt (" << nTokens
                << "tokens) does not fit in context (n_ctx =" << nCtx << ")";
    return QString();
  }

  // Decode the prompt in n_batch-sized chunks via the extended batch API
  // (llama_batch_ext + llama_process), which replaced llama_batch/llama_decode
  // in llama.cpp 0.6.0.  Output logits are requested only on the very last
  // token of the whole prompt (the sampler reads the final position).
  common_batch batch(m_ctx);
  llama_pos pos = 0;
  while (pos < nTokens) {
    const llama_pos end = std::min(pos + (llama_pos)m_n_batch, nTokens);
    for (llama_pos i = pos; i < end; ++i) {
      if (batch.add(tokens[i], i, 0, /*output=*/i == nTokens - 1) < 0) {
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

  return generate(nTokens, nTokens);
}

// ── Multimodal inference ──────────────────────────────────────────
// Migrated to the llama.cpp 0.6.0 multimodal API (see tools/mtmd/mtmd-cli.cpp):
//   - build chunks with mtmd_tokenize_from_parts (per-part parse_special)
//   - text chunks  -> mtmd_helper_eval_chunk_single
//   - media chunks -> mtmd_batch_encode + mtmd_helper_decode_image_chunk
// The deprecated mtmd_helper_eval_chunks / llama_decode path was removed.

QString InferenceEngine::inferMultimodal(const common_chat_prompt &prompt,
                                         const std::vector<PageImage> &images,
                                         struct mtmd_context *mctx) {
  clearKvCache();

  if (!mctx || !mtmd_support_vision(mctx)) {
    qWarning() << "[InferenceEngine] Multimodal requested but vision "
                  "not available";
    return QString();
  }

  mtmd::input_chunks_ptr chunks(mtmd_input_chunks_init());
  if (!tokenizeMultimodal(mctx, prompt, images, chunks.get()))
    return QString();

  const size_t nChunks = mtmd_input_chunks_size(chunks.get());
  const size_t nTokens = mtmd_helper_get_n_tokens(chunks.get());
  const auto nCtx = static_cast<size_t>(llama_n_ctx(m_ctx));
  qDebug() << "[InferenceEngine] mtmd chunks:" << nChunks
           << "| tokens:" << nTokens << "/ n_ctx:" << nCtx;
  if (nTokens >= nCtx) {
    qCritical() << "[InferenceEngine] Multimodal prompt (" << nTokens
                << "tokens) does not fit in context (n_ctx =" << nCtx << ")";
    return QString();
  }

  // Logits for the first sampled token are only produced by the final text
  // chunk; a prompt ending on a media chunk would leave none to sample from.
  if (nChunks == 0 ||
      mtmd_input_chunk_get_type(mtmd_input_chunks_get(
          chunks.get(), nChunks - 1)) != MTMD_INPUT_CHUNK_TYPE_TEXT) {
    qCritical() << "[InferenceEngine] Multimodal prompt must end with text "
                   "(e.g. the assistant turn prefix)";
    return QString();
  }

  llama_pos nPast = 0;
  mtmd::batch_ptr mbatch; // lazily created for runs of consecutive media chunks

  for (size_t i = 0; i < nChunks; ++i) {
    auto chunk = mtmd_input_chunks_get(chunks.get(), i);

    if (mtmd_input_chunk_get_type(chunk) == MTMD_INPUT_CHUNK_TYPE_TEXT) {
      // A text chunk terminates the current media batch.
      mbatch.reset();
      llama_pos newNpast = nPast;
      int32_t res = mtmd_helper_eval_chunk_single(
          mctx, m_ctx, chunk, nPast, 0, m_n_batch, i == nChunks - 1, &newNpast);
      if (res != 0) {
        qCritical() << "[InferenceEngine] eval text chunk" << i
                    << "failed:" << res;
        return QString();
      }
      nPast = newNpast;
      continue;
    }

    // Media chunk: batch-encode it, then decode the resulting embeddings.
    float *embd =
        mbatch ? mtmd_batch_get_output_embd(mbatch.get(), chunk) : nullptr;
    if (!embd) {
      // Start (or restart) a media batch and pack in as many consecutive
      // media chunks as possible before the next text chunk.
      mbatch.reset(mtmd_batch_init(mctx));
      if (mtmd_batch_add_chunk(mbatch.get(), chunk) != 0) {
        qCritical() << "[InferenceEngine] mtmd_batch_add_chunk failed for" << i;
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
        return QString();
      }
      embd = mtmd_batch_get_output_embd(mbatch.get(), chunk);
    }
    if (!embd) {
      qCritical() << "[InferenceEngine] no embeddings for media chunk" << i;
      return QString();
    }

    llama_pos newNpast = nPast;
    int32_t res = mtmd_helper_decode_image_chunk(mctx, m_ctx, chunk, embd,
                                                 nPast, 0, m_n_batch,
                                                 &newNpast, nullptr, nullptr);
    if (res != 0) {
      qCritical() << "[InferenceEngine] decode image chunk" << i
                  << "failed:" << res;
      return QString();
    }
    nPast = newNpast;
  }

  qDebug() << "[InferenceEngine] Multimodal prompt decoded, n_past=" << nPast;
  // Pass the mtmd-computed n_past through: for M-RoPE models it advances by
  // the image's n_pos, fewer than its tokens.  The KV cells actually used
  // are the token count, which is what bounds the generation budget.
  return generate(nPast, static_cast<int32_t>(nTokens));
}

// ── Shared generation loop ────────────────────────────────────────

QString InferenceEngine::generate(llama_pos nPast, int32_t nCells) {
  if (!m_sampler) {
    qCritical() << "[InferenceEngine] No sampler available";
    return QString();
  }

  // Token budget: the free KV cells. LocalBackend sizes n_ctx as
  // prompt_tokens + response headroom, so this is effectively the headroom.
  // Each generated token takes one cell, so the budget is counted in cells,
  // not positions (nPast < nCells for M-RoPE models with images).
  const auto nCtx = (llama_pos)llama_n_ctx(m_ctx);
  const int32_t limit = std::max<int32_t>(0, nCtx - nCells);
  qDebug() << "[InferenceEngine] Generating | n_past:" << nPast
           << "| kv cells:" << nCells << "| n_ctx:" << nCtx
           << "| token budget:" << limit;

  // Raw UTF-8 bytes of the whole output. A single token may hold an
  // incomplete UTF-8 sequence (byte-fallback tokens, emoji, CJK...), so we
  // only convert to QString once at the end, and stream complete
  // characters only via tokenGenerated.
  std::string output;
  size_t emitted = 0; // bytes of `output` already emitted as tokenGenerated

  // Poll for a queued stop() every kPollTokens generated tokens. The
  // generation loop is synchronous and blocks the worker's event loop, so a
  // queued stop() slot cannot run until we pump events; this is what makes
  // cooperative cancellation actually interrupt a long local generation.
  const int kPollTokens = 32;

  common_batch batch(m_ctx);
  int32_t i = 0;
  while (true) {
    if (m_cancelFlag && *m_cancelFlag) {
      qInfo() << "[InferenceEngine] Generation cancelled after" << i
              << "tokens";
      break;
    }
    if (i >= limit) {
      qWarning() << "[InferenceEngine] Context full after" << i
                 << "tokens (n_ctx:" << nCtx << ") — output truncated";
      break;
    }

    llama_token tokenId = common_sampler_sample(m_sampler.get(), m_ctx, -1);
    common_sampler_accept(m_sampler.get(), tokenId, true);

    if (llama_vocab_is_eog(m_vocab, tokenId)) {
      qDebug() << "[InferenceEngine] Generation stopped on EOS after" << i
               << "tokens | final position:" << (nPast + i);
      break;
    }

    output += tokenToPiece(tokenId);

    const size_t complete = utf8CompletePrefixLen(output);
    if (complete > emitted) {
      emit tokenGenerated(
          QString::fromUtf8(output.data() + emitted, complete - emitted));
      emitted = complete;
    }

    // Decode the single token via llama_batch_ext + llama_process
    // (replaces the removed llama_decode path).
    batch.clear();
    if (batch.add(tokenId, nPast + i, 0, /*output=*/true) < 0) {
      // Without a decode the logits are stale and sampling would loop
      // forever on them — abort instead.
      qCritical() << "[InferenceEngine] Failed to add generated token" << i
                  << "to batch | position:" << (nPast + i)
                  << "— output truncated";
      break;
    }
    if (llama_process(m_ctx, LLAMA_PROCESS_TYPE_DECODE, batch.get()) != 0) {
      qCritical() << "[InferenceEngine] Decode failed at token" << i
                  << "| position:" << (nPast + i) << "/ n_ctx:" << nCtx
                  << "— output truncated";
      break;
    }
    ++i;

    if (m_cancelFlag && (i % kPollTokens == 0))
      QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 0);
  }

  // Any trailing incomplete sequence (generation cut mid-character) becomes
  // a single U+FFFD here rather than corrupting every split character.
  return QString::fromUtf8(output.data(), static_cast<qsizetype>(output.size()));
}

// ── Helpers ────────────────────────────────────────────────────────

/// Returns raw bytes: a piece may be an incomplete UTF-8 sequence, so it
/// must not be converted to QString on its own.
std::string InferenceEngine::tokenToPiece(llama_token tok) const {
  return common_token_to_piece(m_ctx, tok, true);
}
