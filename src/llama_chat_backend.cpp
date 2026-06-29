#include "headers/llama_chat_backend.h"

#include "llama.h"
#include "mtmd-helper.h"
#include "mtmd.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QThread>
#include <array>
#include <cstring>
#include <vector>

/// Static — llama_backend_init is called once on first construction.
static bool s_llamaInitialized = false;

LlamaChatBackend::LlamaChatBackend(QObject *parent) : QObject(parent) {
  if (!s_llamaInitialized) {
    // Suppress llama.cpp / mtmd log spam — only show warnings+.
    llama_log_set(
        [](enum ggml_log_level level, const char *text, void *) {
          if (level >= GGML_LOG_LEVEL_WARN) {
            fprintf(stderr, "llama.cpp: %s", text);
          }
        },
        nullptr);
    mtmd_helper_log_set(
        [](enum ggml_log_level level, const char *text, void *) {
          if (level >= GGML_LOG_LEVEL_WARN) {
            fprintf(stderr, "mtmd: %s", text);
          }
        },
        nullptr);

    llama_backend_init();
    s_llamaInitialized = true;
    qDebug() << "[LlamaChatBackend] llama backend initialized"
             << llama_print_system_info();
  }
}

LlamaChatBackend::~LlamaChatBackend() { unloadModel(); }

// ── Model / mmproj loading ─────────────────────────────────────────

bool LlamaChatBackend::loadModel(const QString &modelPath) {
  unloadModel();

  QString path = resolveModelPath(modelPath);
  if (path.isEmpty() || !QFile::exists(path)) {
    qCritical() << "[LlamaChatBackend] Model file not found:" << path;
    emit statusUpdated("Model file not found: " + path);
    return false;
  }

  emit statusUpdated("Loading model: " + path);

  llama_model_params modelParams = llama_model_default_params();
  modelParams.n_gpu_layers = 99;
  modelParams.progress_callback = nullptr;

  m_llamaModel =
      llama_model_load_from_file(path.toUtf8().constData(), modelParams);
  if (!m_llamaModel) {
    qCritical() << "[LlamaChatBackend] Failed to load model on GPU. "
                << "Retrying on CPU...";
    emit statusUpdated("GPU load failed — retrying on CPU");

    modelParams.n_gpu_layers = 0;
    m_llamaModel =
        llama_model_load_from_file(path.toUtf8().constData(), modelParams);
    if (!m_llamaModel) {
      qCritical() << "[LlamaChatBackend] Failed to load model on CPU too:"
                  << path;
      emit statusUpdated("Failed to load model");
      return false;
    }
    qDebug() << "[LlamaChatBackend] Model loaded on CPU (no GPU offload)";
  }

  bool useGpu = (modelParams.n_gpu_layers > 0);

  llama_context_params ctxParams = llama_context_default_params();
  ctxParams.n_ctx = MaxPromptTokens + MaxGenTokens;
  ctxParams.n_batch = MaxPromptTokens;
  ctxParams.n_ubatch = MaxPromptTokens;
  ctxParams.n_threads = QThread::idealThreadCount();
  ctxParams.n_threads_batch = -1;
  if (useGpu) {
    ctxParams.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_ENABLED;
  }

  m_llamaCtx = llama_init_from_model(m_llamaModel, ctxParams);
  if (!m_llamaCtx) {
    qCritical() << "[LlamaChatBackend] Failed to create context";
    emit statusUpdated("Failed to create context");
    llama_model_free(m_llamaModel);
    m_llamaModel = nullptr;
    return false;
  }

  m_modelPath = path;

  // ── Try to load mmproj for multimodal support ──
  m_mmprojPath = resolveMmprojPath(path);
  if (!m_mmprojPath.isEmpty()) {
    emit statusUpdated("Loading multimodal projector: " + m_mmprojPath);

    auto mparams = mtmd_context_params_default();
    mparams.use_gpu = useGpu;
    mparams.n_threads = QThread::idealThreadCount();
    if (useGpu) {
      mparams.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_ENABLED;
    }
    mparams.warmup = false;

    m_mtmdCtx = mtmd_init_from_file(m_mmprojPath.toUtf8().constData(),
                                    m_llamaModel, mparams);
    if (m_mtmdCtx) {
      m_hasVision = mtmd_support_vision(m_mtmdCtx);
      qDebug() << "[LlamaChatBackend] Vision support:" << m_hasVision;
      if (m_hasVision) {
        emit statusUpdated("Multimodal projector loaded (vision)");
      }
    } else {
      qWarning() << "[LlamaChatBackend] Failed to load mmproj:" << m_mmprojPath;
    }
  }

  m_ready = true;
  uint32_t ctxSize = llama_n_ctx(m_llamaCtx);
  qDebug() << "[LlamaChatBackend] Model loaded. ctx=" << ctxSize
           << "| vision=" << m_hasVision;
  emit statusUpdated("Model loaded: " + path);
  return true;
}

void LlamaChatBackend::unloadModel() {
  if (m_mtmdCtx) {
    mtmd_free(m_mtmdCtx);
    m_mtmdCtx = nullptr;
  }
  m_hasVision = false;
  if (m_llamaCtx) {
    llama_free(m_llamaCtx);
    m_llamaCtx = nullptr;
  }
  if (m_llamaModel) {
    llama_model_free(m_llamaModel);
    m_llamaModel = nullptr;
  }
  m_ready = false;
  m_modelPath.clear();
  m_mmprojPath.clear();
}

bool LlamaChatBackend::isReady() const { return m_ready; }

// ── Summarize entry point ──────────────────────────────────────────

QString LlamaChatBackend::summarize(const PromptPayload &payload) {
  if (!m_ready) {
    qWarning() << "[LlamaChatBackend] Model not loaded";
    return QString();
  }

  // Build the formatted prompt text, clamped to context size.
  // Reserve ~3 chars/tokens; leave room for system prompt + generation.
  int32_t ctxSize = llama_n_ctx(m_llamaCtx);
  int32_t maxPromptToks = std::max(1, ctxSize - MaxGenTokens);
  int maxChars = maxPromptToks * 3;

  QString promptText =
      buildFormattedPrompt(payload, m_hasVision ? maxChars : maxChars);

  return runInference(promptText, payload.visualContext);
}

// ── Text-only generate (public API, used by callers without images) ─

QString LlamaChatBackend::generate(const QString &systemPrompt,
                                   const QString &userPrompt) {
  PromptPayload p;
  p.systemPrompt = systemPrompt;
  p.userMessage = userPrompt;
  p.textContext = userPrompt;
  return summarize(p);
}

// ── Helpers ────────────────────────────────────────────────────────

QString LlamaChatBackend::resolveModelPath(const QString &modelPath) const {
  if (modelPath.isEmpty()) {
    return QCoreApplication::applicationDirPath() +
           "/Models/Summarizers/default.gguf";
  }
  if (QFile::exists(modelPath))
    return modelPath;
  QString appPath = QCoreApplication::applicationDirPath() + "/" + modelPath;
  if (QFile::exists(appPath))
    return appPath;
  return modelPath;
}

QString LlamaChatBackend::resolveMmprojPath(const QString &modelPath) const {
  // Strategy: look for <model>-mmproj.gguf next to the model.
  QString dir = QFileInfo(modelPath).absoluteDir().absolutePath();
  QString stem = QFileInfo(modelPath).completeBaseName();

  QStringList candidates;
  candidates << dir + "/" + stem + "-mmproj.gguf"
             << dir + "/" + stem + ".mmproj.gguf" << dir + "/mmproj.gguf";

  // Also check the Models/Mmproj directory.
  QString appDir = QCoreApplication::applicationDirPath();
  candidates << appDir + "/Models/Mmproj/" + stem + "-mmproj.gguf"
             << appDir + "/Models/Mmproj/mmproj.gguf";

  for (const QString &c : candidates) {
    if (QFile::exists(c)) {
      qDebug() << "[LlamaChatBackend] Found mmproj:" << c;
      return c;
    }
  }

  qDebug() << "[LlamaChatBackend] No mmproj found for model:" << modelPath;
  return QString();
}

QString LlamaChatBackend::buildFormattedPrompt(const PromptPayload &payload,
                                               int32_t maxChars) {
  // Build the user message.  If vision is enabled, insert one media marker
  // per image so mtmd_tokenize can replace them.
  const char *marker =
      mtmd_get_marker(m_mtmdCtx) ? mtmd_get_marker(m_mtmdCtx) : "<__media__>";

  QString userPrompt = payload.userMessage + "\n\n";
  userPrompt += "## TEXT CONTEXT\n\n";

  QString ctx = payload.textContext;
  if (ctx.length() > maxChars) {
    ctx = ctx.left(maxChars) + "\n\n... [truncated]";
  }
  userPrompt += ctx;

  // Append media markers for images (mtmd will replace them with image
  // tokens during tokenization).
  if (!payload.visualContext.empty()) {
    userPrompt += "\n\n## VISUAL CONTEXT\n";
    for (size_t i = 0; i < payload.visualContext.size(); ++i) {
      userPrompt += QString("Page %1: %2\n").arg(i + 1).arg(marker);
    }
  }

  // Apply chat template (system + user).
  struct llama_chat_message chat[2];
  int nMsg = 0;

  QByteArray sysBytes = payload.systemPrompt.toUtf8();
  QByteArray userBytes = userPrompt.toUtf8();

  if (!payload.systemPrompt.isEmpty()) {
    chat[nMsg].role = "system";
    chat[nMsg].content = sysBytes.constData();
    nMsg++;
  }
  chat[nMsg].role = "user";
  chat[nMsg].content = userBytes.constData();
  nMsg++;

  // Try built-in template first, then known fallbacks.
  const char *tmpl = llama_model_chat_template(m_llamaModel, nullptr);
  const char *tmplCandidates[] = {tmpl, "chatml", "gemma", nullptr};

  int32_t estSize = 4096;
  for (int i = 0; i < nMsg; i++) {
    estSize += static_cast<int32_t>(strlen(chat[i].content)) + 2048;
  }

  QVarLengthArray<char> promptBuf(static_cast<size_t>(estSize));
  int32_t res = -1;

  for (int t = 0; tmplCandidates[t] != nullptr; t++) {
    promptBuf.resize(static_cast<size_t>(estSize));
    res = llama_chat_apply_template(tmplCandidates[t], chat,
                                    static_cast<size_t>(nMsg), true,
                                    promptBuf.data(), estSize);
    if (res > 0)
      break;
    if (res == -1 && t < 2)
      continue;
    // Buffer too small — resize and retry.
    promptBuf.resize(static_cast<size_t>(-res));
    res = llama_chat_apply_template(
        tmplCandidates[t], chat, static_cast<size_t>(nMsg), true,
        promptBuf.data(), static_cast<int32_t>(promptBuf.size()));
    if (res > 0)
      break;
  }

  if (res <= 0) {
    qWarning() << "[LlamaChatBackend] All chat templates failed (err=" << res
               << ")";
    return QString();
  }

  return QString::fromUtf8(promptBuf.data(), res);
}

// ── Core inference loop ────────────────────────────────────────────

QString
LlamaChatBackend::runInference(const QString &formattedText,
                               const std::vector<PageImage> &pageImages) {
  if (!m_ready)
    return QString();

  const auto *vocab = llama_model_get_vocab(m_llamaModel);
  int32_t ctxSize = llama_n_ctx(m_llamaCtx);
  int32_t maxPromptToks = std::max(1, ctxSize - MaxGenTokens);

  // ── Multimodal path ──
  if (m_hasVision && m_mtmdCtx && !pageImages.empty()) {
    qDebug() << "[LlamaChatBackend] Multimodal inference with"
             << pageImages.size() << "image(s)";

    // Build mtmd bitmaps directly from raw RGB pixels (no encoding overhead).
    std::vector<mtmd_bitmap *> bitmaps;
    bitmaps.reserve(pageImages.size());

    for (size_t i = 0; i < pageImages.size(); ++i) {
      const auto &pi = pageImages[i];
      mtmd_bitmap *bm = mtmd_bitmap_init(pi.width, pi.height, pi.data.data());
      if (bm) {
        bitmaps.push_back(bm);
      } else {
        qWarning() << "[LlamaChatBackend] Failed to create bitmap" << i;
      }
    }

    // Tokenize prompt + images.
    mtmd_input_text text;
    QByteArray fmtBytes = formattedText.toUtf8();
    text.text = fmtBytes.constData();
    text.add_special = true;
    text.parse_special = true;

    auto chunks = mtmd::input_chunks(mtmd_input_chunks_init());
    // Build const pointer array for mtmd_tokenize.
    std::vector<const mtmd_bitmap *> constBitmaps;
    constBitmaps.reserve(bitmaps.size());
    for (auto *bm : bitmaps)
      constBitmaps.push_back(bm);

    int32_t tokRes = mtmd_tokenize(m_mtmdCtx, chunks.ptr.get(), &text,
                                   constBitmaps.data(), constBitmaps.size());
    if (tokRes != 0) {
      qWarning() << "[LlamaChatBackend] mtmd_tokenize failed:" << tokRes;
      for (auto bm : bitmaps)
        mtmd_bitmap_free(bm);
      return QString();
    }

    // Free bitmaps (no longer needed after tokenize).
    for (auto bm : bitmaps)
      mtmd_bitmap_free(bm);

    // Decode each chunk (text via llama_decode, image via mtmd_encode).
    llama_pos nPast = 0;
    size_t nChunks = chunks.size();

    // Batch encode all media chunks first.
    auto mbatch = mtmd::batch_ptr(mtmd_batch_init(m_mtmdCtx));

    for (size_t i = 0; i < nChunks; i++) {
      const mtmd_input_chunk *chunk = chunks[i];
      if (mtmd_input_chunk_get_type(chunk) == MTMD_INPUT_CHUNK_TYPE_IMAGE) {
        mtmd_batch_add_chunk(mbatch.get(), chunk);
      }
    }

    // Actually encode the batched media chunks.
    // (We do a simple single-chunk encode loop since batch add may reject
    //  incompatible chunks).
    // Fallback: encode each image chunk individually.
    for (size_t i = 0; i < nChunks; i++) {
      const mtmd_input_chunk *chunk = chunks[i];
      auto ctype = mtmd_input_chunk_get_type(chunk);

      if (ctype == MTMD_INPUT_CHUNK_TYPE_TEXT) {
        // Text chunk — decode directly.
        const llama_token *tokens = nullptr;
        size_t nTokens = 0;
        tokens = mtmd_input_chunk_get_tokens_text(chunk, &nTokens);

        // Copy to mutable buffer (llama_batch_get_one needs non-const).
        std::vector<llama_token> mutableTokens(tokens, tokens + nTokens);
        llama_batch batch = llama_batch_get_one(mutableTokens.data(),
                                                static_cast<int>(nTokens));
        for (int32_t j = 0; j < batch.n_tokens; j++) {
          batch.pos[j] = nPast;
        }
        batch.logits[batch.n_tokens - 1] = (i == nChunks - 1) ? 1 : 0;

        if (llama_decode(m_llamaCtx, batch) != 0) {
          qWarning() << "[LlamaChatBackend] Text chunk decode failed at" << i;
          llama_batch_free(batch);
          return QString();
        }
        nPast += static_cast<llama_pos>(nTokens);
        llama_batch_free(batch);

      } else {
        // Image chunk — encode + decode embeddings.
        if (mtmd_encode_chunk(m_mtmdCtx, chunk) != 0) {
          qWarning() << "[LlamaChatBackend] Image encode failed at" << i;
          return QString();
        }

        const auto *imageTokens = mtmd_input_chunk_get_tokens_image(chunk);
        size_t nImgToks = mtmd_image_tokens_get_n_tokens(imageTokens);

        float *embdPtr = mtmd_get_output_embd(m_mtmdCtx);
        int32_t nEmbd = llama_model_n_embd_inp(m_llamaModel);

        // batch.embd points to a contiguous buffer of n_tokens * n_embd floats.
        // Copy the image embeddings into it.
        llama_batch batch =
            llama_batch_init(static_cast<uint32_t>(nImgToks), 0, 1);
        batch.n_tokens = static_cast<int32_t>(nImgToks);

        // Copy embedding data: each token gets nEmbd floats.
        if (batch.embd) {
          std::memcpy(batch.embd, embdPtr, nImgToks * nEmbd * sizeof(float));
        }

        for (size_t j = 0; j < nImgToks; j++) {
          batch.token[j] = 0;
          batch.pos[j] = nPast + static_cast<llama_pos>(j);
          batch.seq_id[j][0] = 0;
          batch.n_seq_id[j] = 1;
          batch.logits[j] = 0;
        }
        batch.logits[batch.n_tokens - 1] = (i == nChunks - 1) ? 1 : 0;

        if (llama_decode(m_llamaCtx, batch) != 0) {
          qWarning() << "[LlamaChatBackend] Image chunk decode failed at" << i;
          llama_batch_free(batch);
          return QString();
        }
        nPast += static_cast<llama_pos>(nImgToks);
        llama_batch_free(batch);
      }
    }

    qDebug() << "[LlamaChatBackend] All chunks decoded, n_past=" << nPast;

    // ── Generation loop (same as text-only path) ──
    auto *smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(smpl, llama_sampler_init_top_k(TopK));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_p(TopP, 1));
    if (Temperature > 0.0f) {
      llama_sampler_chain_add(smpl, llama_sampler_init_temp(Temperature));
    } else {
      llama_sampler_chain_add(smpl, llama_sampler_init_greedy());
    }
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(1234));

    std::vector<llama_token> generated;
    generated.reserve(MaxGenTokens);
    bool hitEos = false;

    for (int i = 0; i < MaxGenTokens; i++) {
      llama_token nextTok = llama_sampler_sample(smpl, m_llamaCtx, -1);
      generated.push_back(nextTok);

      if (llama_vocab_is_eog(vocab, nextTok) || i == MaxGenTokens - 1) {
        hitEos = true;
        break;
      }

      llama_batch single = llama_batch_get_one(&nextTok, 1);
      if (llama_decode(m_llamaCtx, single) != 0) {
        qWarning() << "[LlamaChatBackend] Generation decode failed at step"
                   << i;
        llama_batch_free(single);
        break;
      }
      llama_batch_free(single);
    }

    // Detokenize
    QString result;
    for (llama_token t : generated) {
      std::array<char, 32> buf;
      int len = llama_token_to_piece(vocab, t, buf.data(),
                                     static_cast<int32_t>(buf.size()), 0, true);
      if (len > 0) {
        result.append(QString::fromUtf8(buf.data(), len));
      }
    }

    llama_sampler_free(smpl);

    qDebug() << "[LlamaChatBackend] Generated" << generated.size() << "tokens"
             << (hitEos ? "(EOS)" : "(max_tokens)");
    emit statusUpdated(QString("Generated %1 tokens").arg(generated.size()));
    return result;
  }

  // ── Text-only path ──
  QByteArray promptBytes = formattedText.toUtf8();
  int nTok = llama_tokenize(vocab, promptBytes.constData(), promptBytes.size(),
                            nullptr, 0, true, true);
  if (nTok < 0)
    nTok = -nTok;

  std::vector<llama_token> tokens(nTok);
  nTok = llama_tokenize(vocab, promptBytes.constData(), promptBytes.size(),
                        tokens.data(), static_cast<int>(tokens.size()), true,
                        true);
  tokens.resize(nTok);

  if (static_cast<int>(tokens.size()) > maxPromptToks) {
    qWarning() << "[LlamaChatBackend] Truncating prompt from" << tokens.size()
               << "to" << maxPromptToks << "tokens";
    tokens.resize(maxPromptToks);
  }

  auto *smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
  llama_sampler_chain_add(smpl, llama_sampler_init_top_k(TopK));
  llama_sampler_chain_add(smpl, llama_sampler_init_top_p(TopP, 1));
  if (Temperature > 0.0f) {
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(Temperature));
  } else {
    llama_sampler_chain_add(smpl, llama_sampler_init_greedy());
  }
  llama_sampler_chain_add(smpl, llama_sampler_init_dist(1234));

  llama_memory_clear(llama_get_memory(m_llamaCtx), true);

  llama_batch batch =
      llama_batch_get_one(tokens.data(), static_cast<int>(tokens.size()));

  if (llama_decode(m_llamaCtx, batch) != 0) {
    qWarning() << "[LlamaChatBackend] Prompt decode failed";
    llama_sampler_free(smpl);
    llama_batch_free(batch);
    return QString();
  }

  std::vector<llama_token> generated;
  generated.reserve(MaxGenTokens);
  bool hitEos = false;

  for (int i = 0; i < MaxGenTokens; i++) {
    llama_token nextTok = llama_sampler_sample(smpl, m_llamaCtx, -1);
    generated.push_back(nextTok);

    if (llama_vocab_is_eog(vocab, nextTok) || i == MaxGenTokens - 1) {
      hitEos = true;
      break;
    }

    llama_batch single = llama_batch_get_one(&nextTok, 1);
    if (llama_decode(m_llamaCtx, single) != 0) {
      qWarning() << "[LlamaChatBackend] Generation decode failed at step" << i;
      llama_batch_free(single);
      break;
    }
    llama_batch_free(single);
  }

  QString result;
  for (llama_token t : generated) {
    std::array<char, 32> buf;
    int len = llama_token_to_piece(vocab, t, buf.data(),
                                   static_cast<int32_t>(buf.size()), 0, true);
    if (len > 0) {
      result.append(QString::fromUtf8(buf.data(), len));
    }
  }

  llama_sampler_free(smpl);
  llama_batch_free(batch);

  qDebug() << "[LlamaChatBackend] Generated" << generated.size() << "tokens"
           << (hitEos ? "(EOS)" : "(max_tokens)");
  emit statusUpdated(QString("Generated %1 tokens").arg(generated.size()));

  return result;
}
