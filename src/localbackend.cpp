#include "localbackend.h"

#include "inferenceengine.h"

#include "common/chat.h"
#include "common/common.h"
#include "ggml-backend.h"
#include "ggml.h"
#include "llama.h"
#include "mtmd.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QRegularExpression>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <tuple>
#include <vector>

/// Static flag — ensures ggml + llama backends are initialised exactly once
/// across the lifetime of the process, even if multiple backend instances
/// are created.
static bool s_llamaInitialized = false;
/// The ggml backend registry outlives llama_backend_free(), so backends must
/// be loaded only once per process: loading them again would register every
/// device a second time.
static bool s_backendsLoaded = false;

LocalBackend::LocalBackend(QObject *parent) : QObject(parent) {
  ensureRuntime();
}

void LocalBackend::ensureRuntime() {
  if (!s_backendsLoaded) {
    // Auto-detect CUDA, Metal, Vulkan, etc.  In a release build this runs
    // SILENT (ggml_backend_load_all_from_path sets silent=true under NDEBUG),
    // so a backend that fails to load produces no diagnostic at all.  To
    // surface the real reason a GPU backend is missing we explicitly load
    // each candidate non-silent BEFORE the bulk load: load_backend() then
    // reports via the log callback whether the DLL failed to load (with
    // dl_error()), scored 0 ("not supported on this system"), or failed to
    // initialize.  A no-op for backends that are already loadable.
    {
      const QString appDir = QCoreApplication::applicationDirPath();
      // Unversioned names (the deploy step creates lib*.so -> lib*.so.0
      // symlinks next to the binary).  Windows ships ggml-*.dll directly.
      const QStringList candidates = {
#ifdef Q_OS_WIN
          "ggml-cuda.dll", "ggml-vulkan.dll"
#else
          "libggml-cuda.so", "libggml-vulkan.so"
#endif
      };
      for (const QString &c : candidates) {
        const QString p = appDir + "/" + c;
        if (QFile::exists(p))
          ggml_backend_load(p.toUtf8().constData());
      }
    }
    ggml_backend_load_all(); // auto-detect CUDA, Metal, Vulkan, etc.
    // Log all registered backends so we can diagnose GPU detection issues.
    {
      int n = (int)ggml_backend_dev_count();
      for (int i = 0; i < n; i++) {
        struct ggml_backend_dev_props p;
        ggml_backend_dev_get_props(ggml_backend_dev_get(i), &p);
        const char *typeName = "META";
        if (p.type == GGML_BACKEND_DEVICE_TYPE_CPU)
          typeName = "CPU";
        else if (p.type == GGML_BACKEND_DEVICE_TYPE_GPU)
          typeName = "GPU";
        else if (p.type == GGML_BACKEND_DEVICE_TYPE_IGPU)
          typeName = "IGPU";
        else if (p.type == GGML_BACKEND_DEVICE_TYPE_ACCEL)
          typeName = "ACCEL";
        qDebug() << "[LocalBackend] Registered device" << i
                 << "| type:" << typeName
                 << "| name:" << (p.name ? p.name : "?")
                 << "| desc:" << (p.description ? p.description : "?")
                 << "| mem:" << (p.memory_total / (1024 * 1024)) << "MiB total,"
                 << (p.memory_free / (1024 * 1024)) << "MiB free";
      }
      if (n == 0)
        qWarning() << "[LocalBackend] No ggml backends registered — "
                      "check that ggml-cuda.dll / ggml-vulkan.dll are next to "
                      "the exe and the CUDA runtime DLLs are present.";
    }
    s_backendsLoaded = true;
  }
  if (!s_llamaInitialized) {
    llama_backend_init(); // start thread pools / memory allocators
    s_llamaInitialized = true;
  }
}

LocalBackend::~LocalBackend() {
  unloadModel();
  if (s_llamaInitialized) {
    llama_backend_free();
    s_llamaInitialized = false;
  }
}

void LocalBackend::ChatTemplatesDeleter::operator()(
    common_chat_templates *t) const {
  common_chat_templates_free(t);
}

// ── Model / mmproj loading ─────────────────────────────────────────

/// Load a GGUF model and (optionally) detect a multimodal projector.
///
/// Strategy: we ask libcommon to do the heavy lifting (device probing, VRAM
/// fitting, layer offload).  The mmproj is deliberately **not** loaded here —
/// it stays off the GPU until the first multimodal inference call.  This
/// prevents the projector from competing with the model for VRAM during the
/// initial load, which is already the most memory-pressured moment.
std::expected<void, QString>
LocalBackend::loadModel(const QString &modelPath, const PromptPayload &payload,
                        uint32_t responseHeadroom) {
  // Exact-token-counting path: the probe measures the prompt tokens and sizes
  // the KV cache as prompt_tokens + responseHeadroom, so no char heuristic.
  unloadModel();

  QString path = resolveModelPath(modelPath);
  if (path.isEmpty() || !QFile::exists(path)) {
    qCritical() << "[LocalBackend] Model file not found:" << path;
    return std::unexpected{"Model file not found: " + path};
  }

  emit statusUpdated(tr("Loading model: %1").arg(path));

  // ── Detect mmproj (lazy-loaded on first multimodal use) ──────────
  m_mmprojPath = resolveMmprojPath(path);
  m_hasVision = !m_mmprojPath.isEmpty();

  // ── Detect devices (GPU + CPU for hybrid offloading) ─────────────
  if (detectDevices().empty()) {
    qCritical() << "[LocalBackend] No backend devices found";
    return std::unexpected{"No backend devices available"};
  }

  qDebug() << "[LocalBackend] loadModel | path:" << path
           << "| mmproj:" << m_mmprojPath << "| headroom:" << responseHeadroom;

  common_init_result_ptr result =
      loadModelWithDevices(path, m_mmprojPath, &payload, responseHeadroom);
  if (!result) {
    qCritical() << "[LocalBackend] Failed to load model:" << path;
    return std::unexpected{"Failed to load model: " + QString(path)};
  }

  // Cache chat templates — parsed once from GGUF metadata.
  // The model may define a Jinja-based template (most modern models do) or
  // fall back to a built-in named template (e.g. "llama-2", "chatml").
  // Parsing throws on a malformed template: report it as a load failure
  // instead of letting the exception escape the worker thread.
  try {
    m_chatTemplates.reset(
        common_chat_templates_init(result->model(), "").release());
  } catch (const std::exception &e) {
    qCritical() << "[LocalBackend] Invalid chat template:" << e.what();
    return std::unexpected{tr("Invalid chat template in model: %1")
                               .arg(QString::fromUtf8(e.what()))};
  }

  // Raw pointers for the inference engine; m_commonInit keeps them alive.
  m_llamaModel = result->model();
  m_llamaCtx = result->context();
  m_commonInit = std::move(result);

  m_responseHeadroom = responseHeadroom;
  m_modelPath = path;
  m_ready = true;

  qDebug() << "[LocalBackend] Model loaded | ctx:" << llama_n_ctx(m_llamaCtx)
           << "| batch:" << llama_n_batch(m_llamaCtx)
           << "| ubatch:" << llama_n_ubatch(m_llamaCtx)
           << "| vision:" << m_hasVision;
  emit statusUpdated(tr("Model loaded: %1").arg(path));
  return {};
}

// ── Adaptive memory fitting helpers ──────────────────────────────
// Strategy inspired by llama.cpp's load-balancer library: probe the
// model without allocating, estimate per-device memory needs (weights
// + KV cache + compute buffer + mmproj), then iteratively adjust
// n_gpu_layers, n_ctx, KV cache type, and n_ubatch until the config
// fits within the bottleneck GPU's free memory.
// This replaces the "n_gpu_layers=999 + brute force fallback" approach.

/// Bytes of one row of `ne` elements of a ggml type, as ggml allocates it
/// (quantized types are stored in blocks: Q8_0 = 34 bytes / 32 elements,
/// Q4_0 = 18 / 32).  `ne` is rounded up to the block size so ggml_row_size
/// never asserts on an odd embedding width.
static uint64_t lb_row_size(int type, int64_t ne) {
  const auto t = static_cast<ggml_type>(type);
  const int64_t blck = std::max<int64_t>(ggml_blck_size(t), 1);
  return ggml_row_size(t, (ne + blck - 1) / blck * blck);
}

/// Round up to nearest multiple of 256 (llama.cpp pads n_ctx to 256).
static uint32_t lb_pad256(uint32_t v) { return (v + 255U) & ~255U; }

/// Short name of a KV cache type, for logs.
static const char *lb_kv_name(int type) {
  return ggml_type_name(static_cast<ggml_type>(type));
}

/// Estimate KV cache size in bytes for the given configuration.
static uint64_t lb_estimate_kv_cache(int32_t n_embd_k_gqa, int32_t n_embd_v_gqa,
                                     int32_t n_layers, uint32_t n_ctx,
                                     int type_k, int type_v) {
  uint32_t padded_ctx = lb_pad256(n_ctx);
  uint64_t k_bytes = lb_row_size(type_k, n_embd_k_gqa) * padded_ctx;
  uint64_t v_bytes = lb_row_size(type_v, n_embd_v_gqa) * padded_ctx;
  uint64_t raw = (k_bytes + v_bytes) * static_cast<uint64_t>(std::max(n_layers, 0));
  // Empirical correction: the naive row-size formula overestimates the
  // actual CUDA allocation by ~3.8x (measured on RTX 5070 Ti / gemma-4-12B).
  // llama.cpp uses a far more compact internal layout (shared K/V buffers,
  // stride optimisations, GQA-aware packing, and only the active portion of
  // the cache is backed by GPU memory).
  return raw * 26 / 100;
}

/// V cache type actually used for a requested KV type: llama.cpp rejects a
/// quantized V cache without flash attention, so it stays F16 then (see
/// LocalBackend::createInitResult).  A quantized K cache is always allowed.
static int lb_v_type(int type_kv, bool flash_attn) {
  return !flash_attn && ggml_is_quantized(static_cast<ggml_type>(type_kv))
             ? GGML_TYPE_F16
             : type_kv;
}

/// Probe a GGUF model without allocating GPU memory, returning the
/// structural info needed for memory estimation.
struct probe_info {
  int32_t n_layers = 0;
  int32_t n_embd = 0;
  int32_t n_embd_k_gqa = 0;
  int32_t n_embd_v_gqa = 0;
  int32_t n_ctx_train = 0;
  uint64_t model_size = 0;
  uint64_t mmproj_size = 0;   // mmproj weights + compute, all devices
  bool has_mmproj = false;
  bool is_hybrid = false;     // recurrent + attention: loaded without flash attn
  uint32_t prompt_tokens = 0; // text + image tokens
  uint32_t image_tokens = 0;  // image share of prompt_tokens (exact or estimate)
};

/// Fallback tokens per image when the probe cannot count them exactly
/// (projector failed to load on the CPU).  The real count depends on the
/// model and image size; for a page capped at PageImage::kModelMaxPixels:
/// Qwen3.5 ≈ 870, Gemma 4 ≈ 390.  Capped at the projector's maximum when it
/// reports one.  If the real total is higher, summarize() counts exactly and
/// truncates the last pages.
static constexpr int32_t kImageTokenEstimate = 800;

/// Images a payload carries, in prompt order (one per marker).
static std::vector<PageImage> lb_collect_images(const PromptPayload &payload) {
  std::vector<PageImage> images;
  if (!payload.pageBlocks.empty()) {
    for (const auto &pb : payload.pageBlocks)
      if (!pb.visualContext.data.empty())
        images.push_back(pb.visualContext);
  } else {
    images = payload.visualContext; // legacy fallback
  }
  return images;
}

/// System prompt matching what the model will actually receive.
static const QString &lb_system_prompt(const PromptPayload &payload,
                                       bool withImages) {
  return withImages || payload.textOnlySystemPrompt.isEmpty()
             ? payload.systemPrompt
             : payload.textOnlySystemPrompt;
}

/// Assemble the raw user-prompt text from a payload (no chat template, no
/// image embeddings).  This is the single source of truth shared by prompt
/// token-counting and the actual inference prompt, so the media marker and
/// structure can never diverge between the two.
///
/// \param marker    media-placeholder string (the mtmd marker).  Empty for
///                  text-only inference: no marker and no image section is
///                  emitted, so the model never sees a literal placeholder.
/// \param maxChars  hard cap on prompt characters.  0 means "no cap" (used
///                  for token counting).  When set, the document body is
///                  truncated on BOTH the pageBlocks and legacy textContext
///                  paths so the counted prompt matches what gets fed to
///                  inference.
static QString lb_build_user_prompt(const PromptPayload &payload,
                                    const QString &marker, int32_t maxChars) {
  // Reserve room for the userMessage header + truncation suffix so the
  // returned string never exceeds maxChars when a cap is applied.
  int32_t budget = 0;
  if (maxChars > 0) {
    budget = maxChars - static_cast<int32_t>(payload.userMessage.length()) -
             40; // "## DOCUMENT" header + "\n\n... [truncated]" suffix
    if (budget < 256)
      budget = 256;
  }

  QString userPrompt = payload.userMessage + "\n\n## DOCUMENT\n\n";

  // ── Interleaved path (pageBlocks non-empty) ──
  if (!payload.pageBlocks.empty()) {
    for (const auto &pb : payload.pageBlocks) {
      userPrompt += QString("## Page %1\n").arg(pb.pageNumber);
      if (!marker.isEmpty() && !pb.visualContext.data.empty())
        userPrompt += marker + "\n";
      userPrompt += pb.text + "\n\n";
      if (budget > 0 && userPrompt.length() > budget) {
        userPrompt.truncate(budget);
        // Never leave a partial media marker ("<__me") at the cut: it would
        // reach the model as literal text.
        for (qsizetype k = marker.length() - 1; k > 0; --k) {
          if (userPrompt.endsWith(QStringView(marker).left(k))) {
            userPrompt.chop(k);
            break;
          }
        }
        userPrompt += "\n\n... [truncated]";
        break;
      }
    }
  }
  // ── Legacy path (textContext + visualContext) ──
  else {
    if (!marker.isEmpty() && !payload.visualContext.empty()) {
      userPrompt += "## VISUAL CONTEXT\n";
      for (size_t i = 0; i < payload.visualContext.size(); ++i)
        userPrompt += QString("Page %1: %2\n").arg(i + 1).arg(marker);
      userPrompt += "\n";
    }
    // userMessage is already at the top of the prompt — don't repeat it.
    userPrompt += "## TEXT CONTEXT\n\n";
    QString ctx = payload.textContext;
    if (budget > 0 && ctx.length() > budget) {
      ctx = ctx.left(budget) + "\n\n... [truncated]";
    }
    userPrompt += ctx;
  }
  return userPrompt;
}

/// Wrap a system + user message in the model's chat template, with thinking
/// disabled — prevents <|channel>thought tags from reasoning models like
/// Gemma4 which break JSON output extraction. Shared by token counting and
/// inference so both see the same prompt. The result keeps template parts
/// and user-input parts apart (see InferenceEngine::tokenizePrompt). May
/// throw on a malformed Jinja template.
static common_chat_prompt lb_apply_chat_template(const common_chat_templates *tmpls,
                                          const QString &systemPrompt,
                                          const QString &userMessage) {
  common_chat_templates_inputs inputs;
  inputs.use_jinja = true;
  inputs.add_generation_prompt = true;
  inputs.enable_thinking = false;

  if (!systemPrompt.isEmpty()) {
    common_chat_msg sysMsg;
    sysMsg.role = "system";
    sysMsg.content = systemPrompt.toStdString();
    inputs.messages.push_back(std::move(sysMsg));
  }

  common_chat_msg usrMsg;
  usrMsg.role = "user";
  usrMsg.content = userMessage.toStdString();
  inputs.messages.push_back(std::move(usrMsg));

  return common_chat_templates_apply(tmpls, inputs).prompt;
}

/// Probe model metadata (no GPU allocation) and optionally measure mmproj.
/// Uses no_alloc=true with mmap disabled to avoid the GGML_ASSERT in
/// llama-model.cpp:1534 which fires when both are enabled simultaneously.
/// When a payload is provided, the exact prompt token count is measured with
/// the same no-alloc model load (no extra load).
static probe_info lb_probe_model(const QString &modelPath,
                                 const QString &mmprojPath,
                                 const PromptPayload *payload) {
  probe_info info{};

  auto mparams = llama_model_default_params();
  mparams.no_alloc = true;
  mparams.load_mode =
      LLAMA_LOAD_MODE_NONE; // required: mmap + no_alloc triggers GGML_ASSERT

  auto *model =
      llama_model_load_from_file(modelPath.toUtf8().constData(), mparams);
  if (!model) {
    qCritical() << "[LocalBackend] lb_probe: cannot load model for probing";
    return info;
  }

  info.n_layers = llama_model_n_layer(model);
  info.n_embd = llama_model_n_embd(model);
  info.n_ctx_train = llama_model_n_ctx_train(model);
  {
    int n_head = std::max(llama_model_n_head(model), 1);
    int n_head_kv = std::max(llama_model_n_head_kv(model), 1);
    info.n_embd_k_gqa = info.n_embd / n_head * n_head_kv;
    info.n_embd_v_gqa = info.n_embd_k_gqa;
  }
  info.model_size = llama_model_size(model);
  info.is_hybrid = llama_model_is_hybrid(model);

  // Count the exact text tokens while the no-alloc model is already open.
  // Images are only part of the prompt when a projector exists; their exact
  // cost is measured below with a temporary CPU projector.
  const bool vision = !mmprojPath.isEmpty();
  const std::vector<PageImage> images =
      vision && payload ? lb_collect_images(*payload) : std::vector<PageImage>{};
  common_chat_prompt prompt;
  if (payload) {
    // No char cap (0) — counting needs the full prompt.
    const QString userPrompt = lb_build_user_prompt(
        *payload, vision ? QString::fromUtf8(mtmd_default_marker()) : QString(),
        0);
    try {
      auto tmpl = common_chat_templates_init(model, "");
      if (tmpl)
        prompt = lb_apply_chat_template(
            tmpl.get(), lb_system_prompt(*payload, !images.empty()),
            userPrompt);
    } catch (const std::exception &e) {
      // Malformed Jinja template: count the raw prompt instead; loadModel
      // reports the template error when it initialises the real templates.
      qWarning() << "[LocalBackend] lb_probe: chat template failed:"
                 << e.what();
    }
    if (prompt.parts.empty())
      prompt.append(userPrompt.toStdString(), /*is_input=*/true);
    info.prompt_tokens = static_cast<uint32_t>(
        InferenceEngine::tokenizePrompt(llama_model_get_vocab(model), prompt)
            .size());
  }

  // Exact image cost: an image's token count depends on the model and the
  // image size (Qwen3.5: ~1 token per 1000 px, Gemma 4: ~0.43),
  // and only an mtmd context can compute it.  Load the projector on the CPU
  // (no VRAM, so the fit below is undisturbed), tokenize the full prompt —
  // image preprocessing only, the encoder never runs — and free it.  The
  // mtmd context needs the text model for metadata only (embedding width,
  // vocab), so the no-alloc probe model is enough.  ~1 GiB of RAM for a
  // moment; the GPU load in loadMmproj() then reads the file from the OS
  // cache.  Falls back to the per-image estimate below on failure.
  bool imagesCounted = false;
  if (!images.empty() && !prompt.parts.empty()) {
    QElapsedTimer timer;
    timer.start();
    auto cparams = mtmd_context_params_default();
    cparams.use_gpu = false;
    cparams.warmup = false;
    cparams.print_timings = false;
    cparams.n_threads = QThread::idealThreadCount();
    mtmd::context_ptr mctx(
        mtmd_init_from_file(mmprojPath.toUtf8().constData(), model, cparams));
    const int32_t total =
        mctx ? InferenceEngine::countMultimodalTokens(mctx.get(), prompt, images)
             : -1;
    if (total > 0) {
      const uint32_t textTokens = info.prompt_tokens;
      info.prompt_tokens = static_cast<uint32_t>(total);
      info.image_tokens =
          info.prompt_tokens > textTokens ? info.prompt_tokens - textTokens : 0;
      imagesCounted = true;
      qDebug() << "[LocalBackend] lb_probe: exact multimodal count:" << total
               << "tokens (" << images.size() << "image(s) ≈"
               << info.image_tokens << "tokens) in" << timer.elapsed() << "ms";
    } else {
      qWarning() << "[LocalBackend] lb_probe: exact image count failed"
                 << (mctx ? "(tokenize)" : "(mmproj load)")
                 << "— using the per-image estimate";
    }
  }

  llama_model_free(model);

  // Measure the mmproj without allocating it: weights + compute buffers per
  // device, and the maximum tokens one image can take.
  if (vision) {
    const mtmd_memory_usage usage = mtmd_get_memory_usage(
        mmprojPath.toUtf8().constData(), mtmd_context_params_default());
    for (const auto &[dev, bytes] : usage.backend_mem_usage)
      info.mmproj_size += bytes;
    if (info.mmproj_size == 0) {
      // Unreadable by mtmd: fall back to the file size as a weight estimate.
      info.mmproj_size = static_cast<uint64_t>(QFileInfo(mmprojPath).size());
    }
    info.has_mmproj = info.mmproj_size > 0;

    if (info.has_mmproj && !images.empty() && !imagesCounted) {
      const int32_t perImage =
          usage.image_max_tokens > 0
              ? std::min(usage.image_max_tokens, kImageTokenEstimate)
              : kImageTokenEstimate;
      info.image_tokens = static_cast<uint32_t>(images.size() * perImage);
      info.prompt_tokens += info.image_tokens;
      qDebug() << "[LocalBackend] lb_probe:" << images.size() << "image(s) ×"
               << perImage << "tokens (estimate, max per image"
               << usage.image_max_tokens << ")";
    }
  }

  qDebug() << "[LocalBackend] lb_probe:" << info.n_layers << "layers,"
           << info.n_embd << "embd," << (info.model_size >> 20) << "MiB"
           << "| mmproj:" << (info.mmproj_size >> 20) << "MiB"
           << "| vision:" << info.has_mmproj;
  return info;
}

/// Per-device memory snapshot.
struct device_mem {
  ggml_backend_dev_t dev = nullptr;
  QString name;
  QString description;
  uint64_t memory_free = 0;
  uint64_t memory_total = 0;
  bool is_gpu = false;
  bool is_integrated = false; // true for iGPU / shared-memory GPU
};

/// Fallback guess from the device name and description, for backends that
/// don't say whether a GPU is integrated.  The ggml device *name* is often
/// just "Vulkan0" / "CUDA0": the product name is in the description, e.g.
/// "AMD Radeon Graphics (RADV RENOIR)".
static bool lb_name_looks_integrated(const QString &name,
                                     const QString &description) {
  const QString s = (name + " " + description).toLower();

  // NVIDIA: only the Jetson SoCs are integrated (GeForce MX, NVS, RTX
  // laptop GPUs all have dedicated VRAM).
  static const QRegularExpression nvSoc(
      QStringLiteral("\\b(tegra|orin|xavier)\\b"));
  if (nvSoc.match(s).hasMatch())
    return true;
  if (s.contains("nvidia") || s.contains("geforce") || s.contains("quadro") ||
      s.contains("tesla"))
    return false;

  // Intel: discrete Arc carries a model number ("Arc A770", "Arc B580",
  // "Arc Pro A60"); Meteor/Lunar Lake iGPUs are plain "Arc Graphics", older
  // ones UHD / Iris / HD Graphics.
  if (s.contains("intel")) {
    static const QRegularExpression arcDiscrete(QStringLiteral(
        "\\barc(\\(tm\\))?\\s+(pro\\s+)?[ab]\\d{2,3}\\b"));
    return !arcDiscrete.match(s).hasMatch();
  }

  // AMD discrete: "RX 7800 XT", "Radeon Pro W7900", "Radeon VII", Instinct.
  static const QRegularExpression amdDiscrete(QStringLiteral(
      "\\b(rx\\s*\\d|radeon(\\(tm\\))? pro\\b|radeon vii|instinct|firepro)"));
  if (amdDiscrete.match(s).hasMatch())
    return false;
  // AMD APUs: "AMD Radeon Graphics", "Radeon 780M Graphics", "Radeon 8060S
  // Graphics", "Radeon Vega 8 Graphics"…
  static const QRegularExpression amdApu(QStringLiteral(
      "radeon(\\(tm\\))?\\s+((\\d{3,4}[ms]|vega(\\s*\\d+)?)\\s+)?graphics"));
  if (amdApu.match(s).hasMatch())
    return true;
  // …or only a RADV / ROCm APU code name.
  static const QRegularExpression amdApuCode(QStringLiteral(
      "\\b(raven2?|picasso|renoir|cezanne|lucienne|barcelo|rembrandt|"
      "mendocino|vangogh|phoenix\\d?|hawk point|strix|krackan|gfx90c|"
      "gfx1103|gfx115[0-3])(?![a-z0-9])")); // "GFX1103_R1" too
  return amdApuCode.match(s).hasMatch();
}

/// Whether a GPU shares system memory.  The backend's own answer comes
/// first: Vulkan maps VkPhysicalDeviceType, CUDA/HIP the device's
/// "integrated" property.  The name heuristic is only a fallback for
/// device types that don't tell; a disagreement is logged, not acted on.
static bool lb_is_integrated_gpu(enum ggml_backend_dev_type type,
                                 const QString &name,
                                 const QString &description) {
  // CPU descriptions such as "AMD Ryzen 5 4600H with Radeon Graphics" would
  // match the APU patterns: never run the heuristic on them.
  if (type == GGML_BACKEND_DEVICE_TYPE_CPU ||
      type == GGML_BACKEND_DEVICE_TYPE_META)
    return false;
  const bool byName = lb_name_looks_integrated(name, description);
  switch (type) {
  case GGML_BACKEND_DEVICE_TYPE_IGPU:
    return true;
  case GGML_BACKEND_DEVICE_TYPE_GPU:
    if (byName)
      qWarning() << "[LocalBackend]" << name << "(" << description
                 << ") is reported as a discrete GPU but its name suggests "
                    "an integrated one — trusting the driver";
    return false;
  default:
    return byName;
  }
}

/// Discover GPU devices with their free memory.  The CPU device is not
/// collected here: CPU paths use ggml_backend_dev_by_type(CPU) directly.
static std::vector<device_mem> lb_discover() {
  std::vector<device_mem> gpuDevs;

  for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
    auto dev = ggml_backend_dev_get(i);
    struct ggml_backend_dev_props props;
    ggml_backend_dev_get_props(dev, &props);

    bool gpu = (props.type == GGML_BACKEND_DEVICE_TYPE_GPU ||
                props.type == GGML_BACKEND_DEVICE_TYPE_IGPU ||
                props.type == GGML_BACKEND_DEVICE_TYPE_ACCEL);

    QString devName = props.name ? QString::fromUtf8(props.name) : "unknown";
    QString devDesc =
        props.description ? QString::fromUtf8(props.description) : "";

    device_mem dm;
    dm.dev = dev;
    dm.name = devName;
    dm.description = devDesc;
    dm.memory_free = props.memory_free;
    dm.memory_total = props.memory_total;
    dm.is_gpu = gpu;
    dm.is_integrated = lb_is_integrated_gpu(props.type, devName, devDesc);

    if (gpu) {
      gpuDevs.push_back(dm);
    } else if (dm.is_integrated ||
               devName.contains("Vulkan", Qt::CaseInsensitive) ||
               devName.contains("RADV", Qt::CaseInsensitive)) {
      // A GPU behind a backend that reports another device type (older
      // ggml-vulkan builds did): include it by name.
      gpuDevs.push_back(dm);
    }
  }
  return gpuDevs;
}

/// Pick the single GPU the model is loaded on (params.devices holds exactly
/// one device).  Preference order: discrete over integrated, CUDA over other
/// backends (the same physical GPU may be exposed by both CUDA and Vulkan),
/// then the most free memory.  Returns nullptr when there is no GPU.
static const device_mem *lb_pick_gpu(const std::vector<device_mem> &gpuDevs) {
  const device_mem *best = nullptr;
  auto key = [](const device_mem &d) {
    return std::tuple(!d.is_integrated,
                      d.name.startsWith("CUDA", Qt::CaseInsensitive),
                      d.memory_free);
  };
  for (const auto &dm : gpuDevs)
    if (!best || key(dm) > key(*best))
      best = &dm;
  return best;
}

/// Silences lb_fit_params' logging while LocalBackend::estimateContext runs
/// it repeatedly (once per candidate context size).
static thread_local bool s_fitQuiet = false;
#define LB_FIT_LOG(fn)                                                         \
  if (s_fitQuiet) {                                                            \
  } else                                                                       \
    fn()

/// Run the adaptive fitting loop. Returns a common_params tuned to fit
/// within the available GPU memory. On failure (no GPU or cannot fit),
/// returns params configured for pure CPU.
static common_params lb_fit_params(const probe_info &probe,
                                   const std::vector<device_mem> &gpuDevs,
                                   const QString &mmprojPath,
                                   uint32_t desiredCtx, uint32_t desiredUbatch,
                                   uint32_t minCtx) {
  common_params params;
  params.n_ctx = desiredCtx;
  params.n_batch = desiredCtx;
  params.n_ubatch = desiredUbatch;

  // No GPU available → CPU only.  Must still fit in available system RAM:
  // a large multimodal model + full-context F16 KV cache can exceed free RAM
  // and crash the process (llama.cpp C-level allocation failure → abort).
  // Mirror the GPU progressive-reduction strategy, but against the CPU's
  // free memory instead of GPU VRAM.
  if (gpuDevs.empty()) {
    params.n_gpu_layers = 0;
    // Available system RAM = min free across CPU devices (usually one).
    // Fallback to a conservative default if none reported.
    uint64_t cpuFree = 16UL * 1024 * 1024 * 1024; // 16 GiB default
    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
      struct ggml_backend_dev_props cp;
      ggml_backend_dev_get_props(ggml_backend_dev_get(i), &cp);
      if (cp.type == GGML_BACKEND_DEVICE_TYPE_CPU &&
          (cpuFree == 0 || cp.memory_free < cpuFree))
        cpuFree = cp.memory_free;
    }
    // Leave headroom for the OS, the Qt GUI, OCR, and transient compute
    // buffers: keep 4 GiB out of play.
    uint64_t cpuMargin = 4UL * 1024 * 1024 * 1024;
    uint64_t cpuAvailable = cpuFree > cpuMargin ? cpuFree - cpuMargin : cpuFree;

    // Persistent CPU demand = model weights + mmproj + KV cache.
    // The mmproj (multimodal projector) is resident once loaded for a
    // visual document, so it must be counted on the CPU path too.
    uint64_t persistentWeights =
        probe.model_size + (probe.has_mmproj ? probe.mmproj_size : 0);

    // Try full context with F16 KV first; if it doesn't fit, shrink n_ctx
    // and/or quantize KV (F16 → Q8_0) until it fits.  Floor n_ctx at the
    // caller's minCtx (4096) — below that multimodal inference is useless.
    // The CPU load always runs without flash attention, so only K can be
    // quantized; V stays F16.
    uint32_t n_ctx = lb_pad256(desiredCtx);
    uint32_t floor_ctx = lb_pad256(std::max(minCtx, 4096U));
    int type_k = GGML_TYPE_F16;
    int type_v = GGML_TYPE_F16;

    uint64_t kv = lb_estimate_kv_cache(probe.n_embd_k_gqa, probe.n_embd_v_gqa,
                                       probe.n_layers, n_ctx, type_k, type_v);
    while (persistentWeights + kv > cpuAvailable) {
      // First try quantizing KV (cheap, ~a quarter of the cache for K only).
      if (type_k == GGML_TYPE_F16) {
        type_k = GGML_TYPE_Q8_0;
        type_v = lb_v_type(type_k, /*flash_attn=*/false);
        kv = lb_estimate_kv_cache(probe.n_embd_k_gqa, probe.n_embd_v_gqa,
                                  probe.n_layers, n_ctx, type_k, type_v);
        continue;
      }
      // Then shrink the context.
      if (n_ctx <= floor_ctx)
        break; // cannot fit even at the floor — accept the risk
      n_ctx = lb_pad256(n_ctx - 1024U);
      kv = lb_estimate_kv_cache(probe.n_embd_k_gqa, probe.n_embd_v_gqa,
                                probe.n_layers, n_ctx, type_k, type_v);
    }

    params.n_ctx = n_ctx;
    params.n_batch = n_ctx;
    params.n_ubatch = desiredUbatch;
    params.cache_type_k = static_cast<ggml_type>(type_k);
    params.cache_type_v = static_cast<ggml_type>(type_v);

    LB_FIT_LOG(qDebug) << "[LocalBackend] lb_fit: no GPU, CPU only"
             << "| n_ctx=" << n_ctx
             << "| kv_type K=" << lb_kv_name(type_k)
             << "V=" << lb_kv_name(type_v) << "| est persistent=" << ((persistentWeights + kv) >> 20) << "MiB"
             << "/ " << (cpuAvailable >> 20) << "MiB available"
             << "| cpuFree=" << (cpuFree >> 20) << "MiB";
    return params;
  }

  // The model is loaded on exactly one GPU (params.devices = {bottleneck}).
  const device_mem *bottleneck = lb_pick_gpu(gpuDevs);

  // 512 MiB safety margin for iGPUs, 2 GiB for discrete GPUs.
  // The CUDA runtime + driver + CUDA graph reserves need significant
  // headroom beyond our weight+KV+compute estimate.  512 MiB was too
  // tight and caused hard GPU OOM crashes during model load.
  bool integrated = bottleneck->is_integrated;
  uint64_t effective_margin =
      integrated ? 512UL * 1024 * 1024       // 512 MiB for iGPU
                 : 2UL * 1024 * 1024 * 1024; // 2 GiB for discrete
  if (bottleneck->memory_free <= effective_margin) {
    params.n_gpu_layers = 0;
    LB_FIT_LOG(qWarning) << "[LocalBackend] lb_fit: GPU" << bottleneck->name
               << "has insufficient free memory, CPU only";
    return params;
  }
  uint64_t available = bottleneck->memory_free - effective_margin;
  if (integrated) {
    LB_FIT_LOG(qDebug) << "[LocalBackend] lb_fit: integrated GPU detected ("
             << bottleneck->name << "), margin=" << (effective_margin >> 20)
             << "MiB, available=" << (available >> 20) << "MiB";
  }

  // Per-layer weight estimate
  uint64_t per_layer = probe.model_size / std::max(probe.n_layers, 1);

  // --- Integrated GPU: all-or-nothing strategy ---
  // Splitting layers between iGPU and CPU triggers
  // GGML_SCHED_MAX_SPLIT_INPUTS overflow in the Vulkan scheduler.
  // Strategy: use n_gpu_layers=999 (proven to work) with full context.
  // We only check that model weights + KV cache fit (the persistent GPU
  // allocations).  The mmproj is lazy-loaded later (excluded from estimate).
  // The compute buffer is transient — freed between decode calls (excluded).
  if (integrated) {
    // Vulkan on AMD radv iGPU is unreliable with large contexts (>30k tokens).
    // Context loss propagates from internal Vulkan threads and cannot be
    // caught with try/catch — it aborts the whole process.  For contexts
    // below 30k tokens the driver is stable enough.
    const uint32_t iGPU_safe_ctx = 30000;
    if (desiredCtx > iGPU_safe_ctx) {
      LB_FIT_LOG(qWarning) << "[LocalBackend] lb_fit: iGPU skipped — context"
                 << desiredCtx << "exceeds safe limit (" << iGPU_safe_ctx
                 << "), falling back to CPU";
      params.n_gpu_layers = 0;
      return params;
    }

    // Use n_gpu_layers=999 (all layers on GPU) — splitting layers between
    // iGPU and CPU triggers GGML_SCHED_MAX_SPLIT_INPUTS overflow in the
    // Vulkan scheduler.  n_batch stays at 8192 regardless of n_ctx to keep
    // Vulkan command buffers manageable.
    params.n_gpu_layers = 999;
    params.n_ctx = desiredCtx;
    params.n_batch = 8192;
    params.n_ubatch = std::max(desiredUbatch / 8, 128U);
    params.devices = {bottleneck->dev};

    // Use Q8_0 KV cache on iGPU to reduce memory pressure (~120 MiB for a
    // 36k context vs ~240 MiB for F16).  Q4_K is not viable — the Vulkan
    // backend cannot execute SET_ROWS on Q4_K view tensors.  Hybrid models
    // run without flash attention, so their V cache stays F16.
    int iGPU_type_k = GGML_TYPE_Q8_0;
    int iGPU_type_v = lb_v_type(iGPU_type_k, !probe.is_hybrid);
    const QString iGPU_kv_name = QString("K=%1 V=%2")
                                     .arg(lb_kv_name(iGPU_type_k))
                                     .arg(lb_kv_name(iGPU_type_v));
    params.cache_type_k = static_cast<ggml_type>(iGPU_type_k);
    params.cache_type_v = static_cast<ggml_type>(iGPU_type_v);

    // Persistent GPU estimate: model weights + KV cache (quantized)
    uint64_t kv_size = lb_estimate_kv_cache(
        probe.n_embd_k_gqa, probe.n_embd_v_gqa, probe.n_layers, desiredCtx,
        iGPU_type_k, iGPU_type_v);
    uint64_t persistent = probe.model_size + kv_size;

    if (persistent <= available) {
      // Fits with full context — done.
      if (probe.has_mmproj) {
        params.mmproj.path = mmprojPath.toStdString();
        params.mmproj_use_gpu = false; // lazy-loaded, not counted
      }
      LB_FIT_LOG(qDebug) << "[LocalBackend] lb_fit: iGPU OK (full params)"
               << "| persistent=" << (persistent >> 20) << "MiB /"
               << (available >> 20) << "MiB"
               << "| model=" << (probe.model_size >> 20) << "MiB"
               << "| kv=" << (kv_size >> 20) << "MiB"
               << "| n_ctx=" << desiredCtx << "| n_ubatch=" << params.n_ubatch
               << "| kv_type" << iGPU_kv_name;
      return params;
    }

    // Doesn't fit — try reducing only n_ctx (weights are fixed).
    // Floor at 4096 — below that multimodal inference is useless.
    uint32_t fit_ctx = lb_pad256(desiredCtx);
    uint32_t floor_ctx = std::max(minCtx, 4096U);

    while (fit_ctx > lb_pad256(floor_ctx)) {
      uint64_t fit_kv = lb_estimate_kv_cache(probe.n_embd_k_gqa,
                                             probe.n_embd_v_gqa, probe.n_layers,
                                             fit_ctx, iGPU_type_k, iGPU_type_v);
      if (probe.model_size + fit_kv <= available) {
        params.n_ctx = fit_ctx;
        params.n_batch = fit_ctx;
        if (probe.has_mmproj) {
          params.mmproj.path = mmprojPath.toStdString();
          params.mmproj_use_gpu = false;
        }
        LB_FIT_LOG(qDebug) << "[LocalBackend] lb_fit: iGPU OK (reduced ctx)"
                 << "| n_ctx=" << fit_ctx
                 << "| persistent=" << ((probe.model_size + fit_kv) >> 20)
                 << "MiB /" << (available >> 20) << "MiB"
                 << "| kv_type" << iGPU_kv_name;
        return params;
      }
      fit_ctx = lb_pad256(fit_ctx - 1024U);
    }

    // Weights alone don't fit → CPU fallback
    LB_FIT_LOG(qWarning) << "[LocalBackend] lb_fit: iGPU cannot fit model ("
               << (probe.model_size >> 20) << "MiB model vs"
               << (available >> 20) << "MiB available), CPU fallback";
    params.n_gpu_layers = 0;
    return params;
  }

  // --- Discrete GPU path ---
  // Reductions, cheapest in quality first; each OOM round applies the first
  // one still possible, then re-estimates:
  //   1. n_ubatch / 2 down to 2048     (smaller compute buffer, slower prompt)
  //   2. KV cache F16 → Q8_0            (half the KV, negligible quality loss)
  //   3. mmproj → CPU                   (slower image encoding only)
  //   4. offload layers up to 30 %      (slower generation, full document)
  //   5. n_ctx −10 % down to minCtx     (document gets truncated)
  //   6. KV cache Q8_0 → Q4_0           (more quality loss)
  //   7. offload the remaining layers
  //   8. n_ubatch / 2 down to 512
  // If nothing fits, the caller loads on CPU.
  const int total_layers = probe.n_layers;
  const int offload_30 = (total_layers * 30 + 99) / 100; // 30 %, rounded up
  int offloaded = 0;                                     // layers on CPU
  uint32_t n_ctx = lb_pad256(desiredCtx);
  const uint32_t ctx_floor = lb_pad256(std::min(minCtx, n_ctx));
  uint32_t n_ubatch = desiredUbatch;
  int type_kv = GGML_TYPE_F16; // K type; V follows unless flash attn is off
  bool mmproj_on_gpu = probe.has_mmproj;
  // Hybrid models run without flash attention (V cache stays F16).
  const bool flash_attn = !probe.is_hybrid;

  for (int attempts = 0; attempts < 500; ++attempts) {
    const int n_gpu_layers = total_layers - offloaded;
    const int type_v = lb_v_type(type_kv, flash_attn);
    // GPU-side demand only: layers on the CPU keep their weights and KV in
    // system RAM (offload_kqv), so they cost nothing here.
    const uint64_t kv_size =
        lb_estimate_kv_cache(probe.n_embd_k_gqa, probe.n_embd_v_gqa,
                             n_gpu_layers, n_ctx, type_kv, type_v);
    const uint64_t gpu_weight = static_cast<uint64_t>(n_gpu_layers) * per_layer;
    const uint64_t mmproj_mem = mmproj_on_gpu ? probe.mmproj_size : 0;
    // CUDA graph reserve allocates per-layer activations.  Calibrated: 8433
    // MiB measured for 48 layers, 8192 ubatch, 3840 embd → ~6 bytes/element.
    // Bumped to 8 for headroom: sliding-window models (gemma4) allocate
    // extra SWA buffers.
    const uint64_t compute_buf = static_cast<uint64_t>(n_ubatch) *
                                 std::max(probe.n_embd, 1) *
                                 static_cast<uint64_t>(n_gpu_layers) * 8;
    const uint64_t gpu_total =
        (gpu_weight + mmproj_mem + kv_size + compute_buf) * 105 / 100;

    if (gpu_total <= available) {
      params.n_gpu_layers = n_gpu_layers;
      params.n_ctx = n_ctx;
      params.n_batch = 8192; // fixed: keeps Vulkan/CUDA command buffers manageable
      params.n_ubatch = n_ubatch;
      params.devices = {bottleneck->dev};
      params.cache_type_k = static_cast<ggml_type>(type_kv);
      params.cache_type_v = static_cast<ggml_type>(type_v);
      if (probe.has_mmproj) {
        params.mmproj.path = mmprojPath.toStdString();
        params.mmproj_use_gpu = mmproj_on_gpu;
      }
      // llama.cpp puts the first n_gpu_layers on the GPU device and the rest
      // on the CPU backend — no need to pass the CPU device explicitly.
      LB_FIT_LOG(qDebug) << "[LocalBackend] lb_fit: OK"
               << (n_gpu_layers == total_layers ? "(GPU only)"
                   : n_gpu_layers > 0           ? "(split GPU/CPU)"
                                                : "(CPU only)")
               << "| n_gpu_layers=" << n_gpu_layers << "/" << total_layers
               << "| n_ctx=" << n_ctx << "| n_ubatch=" << n_ubatch
               << "| kv_type K=" << lb_kv_name(type_kv)
               << "V=" << lb_kv_name(type_v) << "| mmproj on" << (mmproj_on_gpu ? "GPU" : "CPU")
               << "| GPU" << (gpu_total >> 20) << "MiB /" << (available >> 20)
               << "MiB | weights=" << (gpu_weight >> 20)
               << "MiB | kv=" << (kv_size >> 20)
               << "MiB | mmproj=" << (mmproj_mem >> 20)
               << "MiB | compute=" << (compute_buf >> 20) << "MiB";
      return params;
    }

    // ── OOM: apply the first reduction still possible ─────────────
    if (n_ubatch > 2048) { // 1
      const uint32_t old = n_ubatch;
      n_ubatch = std::max(n_ubatch / 2, 2048U);
      LB_FIT_LOG(qDebug) << "[LocalBackend] lb_fit: OOM → n_ubatch" << old << "→"
               << n_ubatch;
    } else if (type_kv == GGML_TYPE_F16) { // 2
      type_kv = GGML_TYPE_Q8_0;
      LB_FIT_LOG(qDebug) << "[LocalBackend] lb_fit: OOM → KV cache F16 → Q8_0";
    } else if (mmproj_on_gpu) { // 3
      mmproj_on_gpu = false;
      LB_FIT_LOG(qDebug) << "[LocalBackend] lb_fit: OOM → mmproj → CPU ("
               << (probe.mmproj_size >> 20) << "MiB freed)";
    } else if (offloaded < offload_30) { // 4
      ++offloaded;
      LB_FIT_LOG(qDebug) << "[LocalBackend] lb_fit: OOM → offload layer" << offloaded
               << "/" << total_layers << "to CPU";
    } else if (n_ctx > ctx_floor) { // 5
      const uint32_t old = n_ctx;
      n_ctx = std::max(lb_pad256(n_ctx - n_ctx / 10), ctx_floor);
      if (n_ctx == old)
        n_ctx = ctx_floor;
      LB_FIT_LOG(qDebug) << "[LocalBackend] lb_fit: OOM → n_ctx" << old << "→" << n_ctx;
    } else if (type_kv == GGML_TYPE_Q8_0) { // 6
      type_kv = GGML_TYPE_Q4_0;
      LB_FIT_LOG(qDebug) << "[LocalBackend] lb_fit: OOM → KV cache Q8_0 → Q4_0";
    } else if (offloaded < total_layers) { // 7
      ++offloaded;
      LB_FIT_LOG(qDebug) << "[LocalBackend] lb_fit: OOM → offload layer" << offloaded
               << "/" << total_layers << "to CPU";
    } else if (n_ubatch > 512) { // 8
      n_ubatch = std::max(n_ubatch / 2, 512U);
      LB_FIT_LOG(qDebug) << "[LocalBackend] lb_fit: OOM → n_ubatch" << n_ubatch;
    } else {
      LB_FIT_LOG(qWarning) << "[LocalBackend] lb_fit: cannot fit on GPU,"
                 << (gpu_total >> 20) << "MiB needed vs" << (available >> 20)
                 << "MiB available";
      break;
    }
  }

  // GPU fitting failed → CPU fallback
  params.n_gpu_layers = 0;
  return params;
}

// ── Backend-specific model loaders ─────────────────────────────────

/// Detect devices and return {gpu, cpu} for hybrid offloading.
/// Priority: CUDA > Vulkan > CPU-only.
/// Returns at least one device (CPU fallback).
std::vector<ggml_backend_dev_t> LocalBackend::detectDevices() {
  ggml_backend_dev_t cudaDev = nullptr;
  ggml_backend_dev_t vulkanDev = nullptr;
  ggml_backend_dev_t cpuDev = nullptr;

  for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
    auto dev = ggml_backend_dev_get(i);
    struct ggml_backend_dev_props p;
    ggml_backend_dev_get_props(dev, &p);
    const char *name = p.name ? p.name : "";
    const char *type_str =
        p.type == GGML_BACKEND_DEVICE_TYPE_CPU
            ? "CPU"
            : (p.type == GGML_BACKEND_DEVICE_TYPE_GPU ? "GPU" : "OTHER");
    qDebug() << "[LocalBackend] Device" << i << "—" << name << "[" << type_str
             << "]";

    if (strstr(name, "CUDA") || strstr(name, "cuda")) {
      if (!cudaDev)
        cudaDev = dev;
    } else if (strstr(name, "Vulkan") || strstr(name, "vulkan")) {
      if (!vulkanDev)
        vulkanDev = dev;
    } else if (p.type == GGML_BACKEND_DEVICE_TYPE_CPU) {
      if (!cpuDev)
        cpuDev = dev;
    }
  }

  std::vector<ggml_backend_dev_t> devs;

  if (cudaDev) {
    qDebug() << "[LocalBackend] Detected CUDA device";
    devs.push_back(cudaDev);
  } else if (vulkanDev) {
    qDebug() << "[LocalBackend] Detected Vulkan device";
    devs.push_back(vulkanDev);
  } else {
    qDebug() << "[LocalBackend] No GPU detected, CPU only";
  }

  // Always add CPU as fallback — llama will auto-split layers that don't
  // fit in VRAM onto the CPU device.
  if (cpuDev) {
    devs.push_back(cpuDev);
  }

  return devs;
}

/// Build a common_init_result.  Never throws: exceptions from
/// common_init_from_params are logged and reported as nullptr.
common_init_result_ptr LocalBackend::createInitResult(common_params &params) {
  // llama_model_params.devices is a NULL-terminated C array of
  // ggml_backend_dev_t*.  common_params.devices is a std::vector, which is
  // NOT NULL-terminated, so common_model_params_to_llama's
  //   mparams.devices = params.devices.data()
  // hands llama an unbounded array.  llama_prepare_model_devices then walks
  //   for (dev = params.devices; *dev; ++dev)
  // reading past the vector's end until it hits a zero byte in the heap.
  // On Windows the MSVC heap has non-zero bytes after the vector, so garbage
  // device pointers get pushed and the load crashes in load_tensors
  // (ggml_backend_dev_get_props on a garbage dev); on Linux the trailing heap
  // happens to be zero so it terminates early and "works".  Append a trailing
  // nullptr so .data() is a properly NULL-terminated array.
  if (!params.devices.empty() && params.devices.back() != nullptr)
    params.devices.push_back(nullptr);

  // llama.cpp rejects a quantized V cache without flash attention
  // ("quantized V cache requires flash_attn to be enabled").  A quantized K
  // cache is fine, so only V falls back to F16.
  auto fixVCacheForNoFa = [&params]() {
    if (params.flash_attn_type == LLAMA_FLASH_ATTN_TYPE_DISABLED &&
        ggml_is_quantized(params.cache_type_v)) {
      qDebug() << "[LocalBackend] Flash attention disabled — V cache"
               << ggml_type_name(params.cache_type_v) << "→ F16";
      params.cache_type_v = GGML_TYPE_F16;
    }
  };

  try {
    fixVCacheForNoFa();
    auto result = common_init_from_params(params);
    if (!result || !result->model() || !result->context())
      return nullptr;

    // Hybrid models (recurrent + attention: Jamba, Granite…) are kept off
    // flash attention.  Only reload when FA could actually be on — the
    // fitted loader already disables it up front, this is a safety net for
    // callers that did not probe the model.
    if (llama_model_is_hybrid(result->model()) &&
        params.flash_attn_type != LLAMA_FLASH_ATTN_TYPE_DISABLED) {
      result.reset();
      params.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_DISABLED;
      qDebug() << "[LocalBackend] Hybrid model — reloading with flash "
                  "attention disabled";
      fixVCacheForNoFa();
      result = common_init_from_params(params);
      if (!result || !result->model() || !result->context())
        return nullptr;
    }
    return result;
  } catch (const std::exception &e) {
    qCritical() << "[LocalBackend] Exception while loading model:" << e.what();
  } catch (...) {
    qCritical() << "[LocalBackend] Unknown exception while loading model";
  }
  return nullptr;
}

/// Model loader: adaptive memory fitting via lb strategy.
///
/// Three-phase approach:
///   1. Probe the model (no GPU allocation) to get layers, embd, size and
///      the exact prompt token count.
///   2. Run lb_fit_params which iteratively adjusts n_gpu_layers,
///      n_ctx, KV cache type, and n_ubatch until it fits the chosen GPU.
///   3. If GPU fitting or the GPU load fails, fall back to pure CPU.
///
/// The mmproj path is stored in params so common_init_from_params knows
/// about it, but the projector is lazy-loaded (not bound to GPU here).
common_init_result_ptr
LocalBackend::loadModelWithDevices(const QString &path,
                                   const QString &mmprojPath,
                                   const PromptPayload *payload,
                                   uint32_t responseHeadroom) {
  // ── Phase 1: probe model metadata (no GPU allocation) ──────────
  // When a payload is given, the probe also counts the exact prompt token
  // count during this same no-alloc load.
  probe_info probe = lb_probe_model(path, mmprojPath, payload);
  if (!probe.n_layers) {
    qCritical() << "[LocalBackend] Model probe failed:" << path;
    return nullptr;
  }

  // ── Phase 2: discover devices and run adaptive fit ─────────────
  const std::vector<device_mem> gpuDevs = lb_discover();
  const bool hasGpu = lb_pick_gpu(gpuDevs) != nullptr;

  // Size the KV cache from the exact prompt token count (measured during the
  // probe) plus the caller's generation headroom.
  uint32_t desiredCtx;
  if (payload && probe.prompt_tokens > 0) {
    desiredCtx = probe.prompt_tokens + responseHeadroom;
    qDebug() << "[LocalBackend] lb_fit: exact prompt tokens:"
             << probe.prompt_tokens << "+ " << responseHeadroom
             << " headroom → desiredCtx:" << desiredCtx;
  } else {
    desiredCtx = probe.n_ctx_train > 0 ? probe.n_ctx_train : 8192;
  }
  if (probe.n_ctx_train > 0 &&
      desiredCtx > static_cast<uint32_t>(probe.n_ctx_train)) {
    desiredCtx = static_cast<uint32_t>(probe.n_ctx_train);
  }
  const uint32_t desiredUbatch = 8192;
  const uint32_t minCtx = 2048;

  // Settings shared by the GPU attempt and the CPU fallback.
  auto applyCommon = [&](common_params &p) {
    p.model.path = path.toStdString();
    p.cpuparams.n_threads = QThread::idealThreadCount();
    p.cpuparams_batch.n_threads = QThread::idealThreadCount();
    // We do our own fitting via lb_fit_params — disable the internal
    // common_fit_params which re-probes all devices and can crash when
    // multiple backends (CUDA + Vulkan) claim the same physical GPU.
    p.fit_params = false;
  };

  // CPU-only load: weights, KV cache and mmproj all on the CPU device.
  auto loadOnCpu = [&](common_params p) -> common_init_result_ptr {
    applyCommon(p);
    p.n_gpu_layers = 0;
    p.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_DISABLED;
    if (auto *cpu = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_CPU))
      p.devices = {cpu};
    else
      p.devices.clear(); // let llama.cpp pick its default
    if (!mmprojPath.isEmpty()) {
      p.mmproj.path = mmprojPath.toStdString();
      p.mmproj_use_gpu = false;
    }
    // The model is on the CPU, so a GPU (if any) is free for the projector.
    m_mmprojUseGpu = hasGpu;
    qDebug() << "[LocalBackend] Loading on CPU | n_ctx=" << p.n_ctx;
    return createInitResult(p);
  };

  common_params params = lb_fit_params(probe, gpuDevs, mmprojPath, desiredCtx,
                                       desiredUbatch, minCtx);


  if (params.n_gpu_layers == 0)
    return loadOnCpu(params);

  // GPU path: the fitter already set params.devices to {chosen GPU}.
  // Do NOT push the CPU device here.  params.devices is the list of
  // compute devices the model is split across; mixing a CPU device (which
  // carries no device_id on Windows — it reports "unknown id") into a
  // fully GPU-offloaded model's device list crashes llama's device
  // scheduler / arch dispatch during llama_model_load_from_file
  // (NULL function pointer, Windows-only).  llama.cpp keeps a CPU backend
  // available via ggml_backend_load_all() for host ops and hybrid offload
  // on its own, so an explicit CPU entry is not needed.
  applyCommon(params);
  // Hybrid models run without flash attention; deciding it here (the fitter
  // sized the V cache accordingly) avoids createInitResult's reload.
  params.flash_attn_type = probe.is_hybrid ? LLAMA_FLASH_ATTN_TYPE_DISABLED
                                           : LLAMA_FLASH_ATTN_TYPE_AUTO;
  if (!mmprojPath.isEmpty() && !probe.has_mmproj) {
    // lb_fit_params only sets mmproj if probe confirmed it exists
    params.mmproj.path = mmprojPath.toStdString();
  }
  // The fitter decided where the projector goes (it reserved its VRAM when
  // on GPU; the iGPU path keeps it on the CPU).
  m_mmprojUseGpu = params.mmproj_use_gpu;
  qDebug() << "[LocalBackend] Loading on GPU with fitted params:"
           << "n_gpu_layers=" << params.n_gpu_layers
           << "n_ctx=" << params.n_ctx << "n_ubatch=" << params.n_ubatch;

  common_init_result_ptr result = createInitResult(params);
  if (!result) {
    qCritical() << "[LocalBackend] GPU load failed, trying CPU fallback";
    return loadOnCpu(lb_fit_params(probe, {}, mmprojPath, desiredCtx,
                                   desiredUbatch, minCtx));
  }
  return result;
}

/// Where a prompt of `promptTokens` (+ headroom) would run, using the same
/// probe and fitting as loadModelWithDevices — without loading anything.
LocalBackend::ContextEstimate
LocalBackend::estimateContext(const QString &modelPath, int promptTokens,
                              uint32_t responseHeadroom) {
  ContextEstimate est;
  if (modelPath.isEmpty() || !QFile::exists(modelPath))
    return est;
  ensureRuntime();

  // The probe reads the GGUF metadata (and measures the projector): cache it
  // per model, the UI re-estimates on every change of the setting.
  static QMutex cacheLock;
  static QHash<QString, probe_info> cache;
  const QString mmprojPath = resolveMmprojPath(modelPath);
  probe_info probe;
  {
    const QMutexLocker locker(&cacheLock);
    const QString key = modelPath + '|' + mmprojPath;
    auto it = cache.constFind(key);
    if (it == cache.constEnd())
      it = cache.insert(key, lb_probe_model(modelPath, mmprojPath, nullptr));
    probe = *it;
  }
  if (!probe.n_layers)
    return est;

  const std::vector<device_mem> gpuDevs = lb_discover();
  const uint32_t ctxLimit =
      probe.n_ctx_train > 0 ? static_cast<uint32_t>(probe.n_ctx_train) : 131072;
  constexpr uint32_t Ubatch = 8192, MinCtx = 2048;

  s_fitQuiet = true;
  // Placement for a prompt budget, mirroring loadModelWithDevices: GPU fit
  // first; a CPU-only result there means the CPU fallback (fitted on RAM).
  auto place = [&](uint32_t prompt, bool *truncated) {
    const uint32_t desired = std::min(prompt + responseHeadroom, ctxLimit);
    common_params p = lb_fit_params(probe, gpuDevs, mmprojPath, desired,
                                    Ubatch, MinCtx);
    Placement where;
    if (gpuDevs.empty() || p.n_gpu_layers == 0) {
      where = Placement::Cpu;
      p = lb_fit_params(probe, {}, mmprojPath, desired, Ubatch, MinCtx);
    } else {
      where = p.n_gpu_layers >= probe.n_layers ? Placement::Gpu
                                               : Placement::Split;
    }
    // The fitter shrinks n_ctx as a last resort; the prompt is then cut.
    const auto nCtx = static_cast<uint32_t>(std::max(p.n_ctx, 0));
    *truncated = nCtx < desired && lb_pad256(desired) > nCtx;
    return where;
  };

  est.maxTokens =
      static_cast<int>(ctxLimit > responseHeadroom ? ctxLimit - responseHeadroom
                                                   : ctxLimit / 2);

  // Auto: the largest budget that runs fully on the GPU, capped at 16k
  // (beyond that a small model rarely uses the extra context well — the
  // user can still raise it).  Without a fitting GPU: 8k on the CPU.
  constexpr int AutoCap = 16384, AutoFloor = 4096, Step = 1024;
  est.autoTokens = 8192;
  for (int b = std::min(AutoCap, est.maxTokens); b >= AutoFloor; b -= Step) {
    bool cut = false;
    if (place(static_cast<uint32_t>(b), &cut) == Placement::Gpu && !cut) {
      est.autoTokens = b;
      break;
    }
  }
  est.autoTokens = std::min(est.autoTokens, est.maxTokens);

  const int requested = promptTokens > 0 ? promptTokens : est.autoTokens;
  est.placement = place(static_cast<uint32_t>(requested), &est.truncated);
  s_fitQuiet = false;

  est.valid = true;
  return est;
}

void LocalBackend::unloadModel() {
  unloadMmproj();
  m_chatTemplates.reset();
  // Resetting the unique_ptr frees the common_init_result, which in turn
  // destroys the llama_model and llama_context (and their GPU buffers).
  m_commonInit.reset();
  m_llamaCtx = nullptr;
  m_llamaModel = nullptr;
  m_hasVision = false;
  m_ready = false;
  m_modelPath.clear();
  m_mmprojPath.clear();

  // Force GPU memory to be released back to the driver before the next
  // model loads — without this the CUDA allocator keeps memory in its
  // internal pool and the new model + mmproj compete for apparent VRAM.
  // We create a temporary backend handle per device, sync, and destroy it.
  // This forces the driver to actually free the cudaMalloc allocations
  // made by the now-dead model context.
  struct ggml_backend_dev_props props;
  for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
    auto dev = ggml_backend_dev_get(i);
    ggml_backend_dev_get_props(dev, &props);
    if (props.type == GGML_BACKEND_DEVICE_TYPE_GPU ||
        props.type == GGML_BACKEND_DEVICE_TYPE_IGPU ||
        props.type == GGML_BACKEND_DEVICE_TYPE_ACCEL) {
      auto backend = ggml_backend_dev_init(dev, nullptr);
      if (backend) {
        ggml_backend_synchronize(backend);
        ggml_backend_free(backend);
        qDebug() << "[LocalBackend] GPU" << i
                 << "synchronized after model unload";
      }
    }
  }
}

bool LocalBackend::isReady() const { return m_ready; }

// ── Summarize entry point ──────────────────────────────────────────

/// Main public entry point. Fits the prompt into the context while keeping
/// the response headroom free, then dispatches to text-only or multimodal
/// inference.
QString LocalBackend::summarize(const PromptPayload &payload,
                                bool *cancelFlag) {
  if (!m_ready) {
    qWarning() << "[LocalBackend] Model not loaded";
    return QString();
  }

  // Token budget for the prompt: the context minus the response headroom.
  // n_ctx was sized at load time as prompt_tokens + headroom, but it can be
  // smaller — capped at n_ctx_train for long documents, or shrunk by the
  // fitter to fit VRAM.  In those cases the prompt must be truncated, or it
  // would fill the whole context and leave nothing to generate into.
  // Headroom is capped at half the context so a tiny n_ctx still gets a
  // usable prompt.
  const int32_t ctxSize = llama_n_ctx(m_llamaCtx);
  const int32_t headroom =
      std::min<int32_t>(static_cast<int32_t>(m_responseHeadroom), ctxSize / 2);
  const int32_t promptBudget = ctxSize - headroom;
  const llama_vocab *vocab = llama_model_get_vocab(m_llamaModel);

  // Vision is used only when the payload has images AND the projector loads.
  // Otherwise the prompt is built without media markers (text-only).
  const std::vector<PageImage> allImages = lb_collect_images(payload);
  bool useVision = false;
  if (!allImages.empty() && m_hasVision) {
    auto r = loadMmproj();
    if (r)
      useVision = true;
    else
      qWarning() << "[LocalBackend] mmproj unavailable, text-only:"
                 << r.error();
  }
  const QString marker =
      useVision ? QString::fromUtf8(mtmd_get_marker(m_mtmdCtx)) : QString();

  // Build the prompt uncapped first (the common case: it fits as counted at
  // load time).  If it doesn't, shrink the character cap in proportion to
  // the token overflow and rebuild.  Truncation drops pages from the end,
  // their markers with them, so only the first `markers` images are kept.
  // Counting uses the exact tokenization of the inference path (image
  // tokens included), so there is no chars-per-token guess.
  int32_t maxChars = 0; // 0 = no cap
  int32_t promptTokens = 0;
  std::optional<common_chat_prompt> prompt;
  std::vector<PageImage> images;
  for (int attempt = 0; attempt < 6; ++attempt) {
    prompt = buildFormattedPrompt(payload, marker, maxChars);
    if (!prompt)
      return QString();

    images.clear();
    if (useVision) {
      const auto nMarkers = static_cast<size_t>(
          QString::fromStdString(prompt->str()).count(marker));
      images.assign(allImages.begin(),
                    allImages.begin() +
                        static_cast<std::ptrdiff_t>(
                            std::min(nMarkers, allImages.size())));
      promptTokens = InferenceEngine::countMultimodalTokens(m_mtmdCtx, *prompt,
                                                            images);
      if (promptTokens < 0)
        return QString();
    } else {
      promptTokens = static_cast<int32_t>(
          InferenceEngine::tokenizePrompt(vocab, *prompt).size());
    }

    if (promptTokens <= promptBudget)
      break;
    const int64_t current =
        maxChars > 0 ? maxChars : QString::fromStdString(prompt->str()).length();
    // 5 % extra margin so the next attempt usually lands under budget.
    maxChars = static_cast<int32_t>(current * promptBudget / promptTokens *
                                    95 / 100);
    qDebug() << "[LocalBackend] Prompt" << promptTokens << "tokens > budget"
             << promptBudget << "→ truncating to" << maxChars << "chars";
  }
  if (promptTokens > promptBudget)
    qWarning() << "[LocalBackend] Prompt still over budget after truncation:"
               << promptTokens << "/" << promptBudget << "tokens";

  qDebug() << "[LocalBackend] summarize() | n_ctx:" << ctxSize
           << "| prompt:" << promptTokens << "tokens | headroom:" << headroom
           << "| pages:" << payload.pageBlocks.size()
           << "| images:" << images.size() << "/" << allImages.size()
           << (useVision ? "(vision)" : "(text-only)");

  if (!images.empty())
    return runMultimodalInference(*prompt, images, cancelFlag);
  return runTextInference(*prompt, cancelFlag);
}

// ── Text-only inference ────────────────────────────────────────────

common_params_sampling LocalBackend::makeSamplingParams() const {
  common_params_sampling params;
  params.temp = 0.1f;
  params.top_k = 40;
  params.top_p = 0.9f;
  params.min_p = 0.05f;
  params.penalty_repeat = 1.1f;
  params.penalty_last_n = 64;
  return params;
}

QString LocalBackend::runTextInference(const common_chat_prompt &prompt,
                                       bool *cancelFlag) {
  InferenceEngine engine(m_llamaCtx, m_llamaModel, makeSamplingParams());
  if (cancelFlag)
    engine.setCancelFlag(cancelFlag);
  QString result = engine.inferText(prompt);

  if (result.isEmpty()) {
    qWarning() << "[LocalBackend] Text inference returned empty output";
  }
  return result;
}

// ── Multimodal inference ──────────────────────────────────────────

/// Run inference with images.  The caller (summarize) has already loaded the
/// mmproj; it stays resident until unloadModel().
QString
LocalBackend::runMultimodalInference(const common_chat_prompt &prompt,
                                     const std::vector<PageImage> &images,
                                     bool *cancelFlag) {
  if (!m_mtmdCtx) {
    qWarning() << "[LocalBackend] Multimodal inference requested but the "
                  "mmproj is not loaded";
    return QString();
  }

  qDebug() << "[LocalBackend] Multimodal inference with" << images.size()
           << "images";

  InferenceEngine engine(m_llamaCtx, m_llamaModel, makeSamplingParams());
  if (cancelFlag)
    engine.setCancelFlag(cancelFlag);
  QString result = engine.inferMultimodal(prompt, images, m_mtmdCtx);

  if (result.isEmpty()) {
    qWarning() << "[LocalBackend] Multimodal inference returned empty output";
  }
  return result;
}

// ── Lazy mmproj load / unload ─────────────────────────────────────

std::expected<void, QString> LocalBackend::loadMmproj() {
  if (m_mtmdCtx)
    return {};
  if (m_mmprojPath.isEmpty())
    return std::unexpected{"No mmproj path configured"};

  emit statusUpdated(tr("Loading multimodal projector: %1").arg(m_mmprojPath));

  const QByteArray path = m_mmprojPath.toUtf8();
  auto mtmdParams = mtmd_context_params_default();
  // Placement decided at model load (fitter / CPU fallback).
  mtmdParams.use_gpu = m_mmprojUseGpu;
  mtmdParams.n_threads = QThread::idealThreadCount();
  mtmdParams.warmup = false;
  mtmdParams.print_timings = false;

  // Models with non-causal attention on images need each image to fit in a
  // single ubatch: cap the per-image token budget to n_ubatch, like
  // llama-server does (fewer tokens = lower image resolution).
  const mtmd_memory_usage usage =
      mtmd_get_memory_usage(path.constData(), mtmdParams);
  const int32_t nUbatch = static_cast<int32_t>(llama_n_ubatch(m_llamaCtx));
  if (usage.use_non_causal && usage.image_max_tokens > nUbatch) {
    qWarning() << "[LocalBackend] Capping image tokens" << usage.image_max_tokens
               << "→" << nUbatch << "(n_ubatch, non-causal image attention)";
    mtmdParams.image_max_tokens = nUbatch;
    mtmdParams.image_min_tokens =
        std::min(mtmdParams.image_min_tokens, nUbatch);
  }

  m_mtmdCtx = mtmd_init_from_file(path.constData(), m_llamaModel, mtmdParams);
  if (!m_mtmdCtx) {
    qWarning() << "[LocalBackend] Failed to load mmproj:" << m_mmprojPath;
    return std::unexpected{tr("Failed to load mmproj: %1").arg(m_mmprojPath)};
  }
  if (!mtmd_support_vision(m_mtmdCtx)) {
    unloadMmproj();
    return std::unexpected{tr("mmproj has no vision encoder: %1")
                               .arg(m_mmprojPath)};
  }

  qDebug() << "[LocalBackend] mmproj loaded on"
           << (mtmdParams.use_gpu ? "GPU" : "CPU");
  return {};
}

void LocalBackend::unloadMmproj() {
  if (m_mtmdCtx) {
    mtmd_free(m_mtmdCtx);
    m_mtmdCtx = nullptr;
    qDebug() << "[LocalBackend] mmproj unloaded";
  }
}

// ── Prompt building ────────────────────────────────────────────────

/// Assemble the user prompt from page blocks in interleaved format:
/// <--image--> <--text--> <--image--> <--text--> …
/// then wrap it in the model's chat template.  `marker` is the mtmd media
/// marker (one per image, replaced by its embeddings), or empty for a
/// text-only prompt.
std::optional<common_chat_prompt>
LocalBackend::buildFormattedPrompt(const PromptPayload &payload,
                                   const QString &marker, int32_t maxChars) {
  // Shared with token-counting (lb_probe_model) so the prompt fed to
  // inference is the one that was counted when sizing the KV cache.
  const QString userPrompt = lb_build_user_prompt(payload, marker, maxChars);
  return applyChatTemplate(lb_system_prompt(payload, !marker.isEmpty()),
                           userPrompt);
}

std::optional<common_chat_prompt>
LocalBackend::applyChatTemplate(const QString &systemPrompt,
                                const QString &userMessage) {
  if (!m_chatTemplates) {
    qWarning() << "[LocalBackend] Chat templates not initialized";
    return std::nullopt;
  }

  common_chat_prompt prompt;
  try {
    prompt = lb_apply_chat_template(m_chatTemplates.get(), systemPrompt,
                                    userMessage);
  } catch (const std::exception &e) {
    qCritical() << "[LocalBackend] Chat template failed:" << e.what();
    return std::nullopt;
  }

  if (prompt.parts.empty()) {
    qWarning() << "[LocalBackend] Chat template produced empty output";
    return std::nullopt;
  }
  return prompt;
}

// ── Path resolution ────────────────────────────────────────────────

/// Resolve model path: try absolute → relative to app dir → default bundled
/// model.
QString LocalBackend::resolveModelPath(const QString &modelPath) const {
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

/// Search for mmproj file: model directory → bundled Mmproj directory.
/// We look for any file containing "mmproj" in the model's directory first,
/// then fall back to well-known paths in the application bundle.
QString LocalBackend::resolveMmprojPath(const QString &modelPath) {
  QString dir = QFileInfo(modelPath).absoluteDir().absolutePath();
  QString stem = QFileInfo(modelPath).completeBaseName();

  QStringList candidates;

  QDir modelDir(dir);
  modelDir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
  for (const QFileInfo &fi : modelDir.entryInfoList()) {
    if (fi.fileName().contains("mmproj", Qt::CaseInsensitive)) {
      candidates << fi.absoluteFilePath();
    }
  }

  QString appDir = QCoreApplication::applicationDirPath();
  candidates << appDir + "/Models/Mmproj/" + stem + "-mmproj.gguf"
             << appDir + "/Models/Mmproj/mmproj.gguf";

  for (const QString &c : candidates) {
    if (QFile::exists(c)) {
      qDebug() << "[LocalBackend] Found mmproj:" << c;
      return c;
    }
  }

  qDebug() << "[LocalBackend] No mmproj found for model:" << modelPath;
  return QString();
}

PageImage PageImage::forModel(const QImage &img) {
  const qint64 pixels = static_cast<qint64>(img.width()) * img.height();
  if (pixels <= kModelMaxPixels)
    return fromQImage(img);
  const double scale = std::sqrt(static_cast<double>(kModelMaxPixels) /
                                 static_cast<double>(pixels));
  return fromQImage(img.scaled(static_cast<int>(img.width() * scale),
                               static_cast<int>(img.height() * scale),
                               Qt::IgnoreAspectRatio,
                               Qt::SmoothTransformation));
}

PageImage PageImage::fromQImage(const QImage &img) {
  PageImage pi;
  const QImage rgb = img.format() == QImage::Format_RGB888
                         ? img
                         : img.convertToFormat(QImage::Format_RGB888);
  pi.width = rgb.width();
  pi.height = rgb.height();
  // Copy row by row: QImage pads every scanline to a 4-byte boundary, so for
  // RGB888 with width*3 not a multiple of 4 the raw buffer contains padding
  // bytes that would shear the image. mtmd expects tightly packed
  // width*height*3 bytes.
  const size_t rowBytes = static_cast<size_t>(pi.width) * 3;
  pi.data.resize(rowBytes * static_cast<size_t>(pi.height));
  for (int y = 0; y < pi.height; ++y)
    std::memcpy(pi.data.data() + rowBytes * y, rgb.constScanLine(y), rowBytes);
  return pi;
}
