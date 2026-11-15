#include "localbackend.h"

#include "inferenceengine.h"
#include "vramfit.h"

#include "common/chat.h"
#include "common/common.h"
#include "ggml-backend.h"
#include "ggml.h"
#include "llama.h"
#include "mtmd.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QThread>
#include <algorithm>
#include <cstring>
#include <optional>
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
  InferenceEngine::logGpuMemory(QStringLiteral("after model load"));
  emit statusUpdated(tr("Model loaded: %1").arg(path));
  return {};
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
    if (!VramFit::kHybridFlashAttn && llama_model_is_hybrid(result->model()) &&
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
///   2. Run VramFit::fitParams which iteratively adjusts n_gpu_layers,
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
  VramFit::ProbeInfo probe = VramFit::probeModel(path, mmprojPath, payload);
  if (!probe.n_layers) {
    qCritical() << "[LocalBackend] Model probe failed:" << path;
    return nullptr;
  }

  // ── Phase 2: discover devices and run adaptive fit ─────────────
  const std::vector<VramFit::DeviceMem> gpuDevs = VramFit::discoverDevices();
  const bool hasGpu = VramFit::pickGpu(gpuDevs) != nullptr;

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
    // We do our own fitting via VramFit::fitParams — disable the internal
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

  common_params params = VramFit::fitParams(probe, gpuDevs, mmprojPath, desiredCtx,
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
  // Decided here, as the fitter assumed (V cache type, KQ matrix), so
  // createInitResult never has to reload.
  params.flash_attn_type = VramFit::usesFlashAttn(probe.is_hybrid)
                               ? LLAMA_FLASH_ATTN_TYPE_AUTO
                               : LLAMA_FLASH_ATTN_TYPE_DISABLED;
  if (!mmprojPath.isEmpty() && !probe.has_mmproj) {
    // VramFit::fitParams only sets mmproj if probe confirmed it exists
    params.mmproj.path = mmprojPath.toStdString();
  }
  // The fitter decided where the projector goes (it reserved its VRAM when
  // on GPU; the iGPU path keeps it on the CPU).
  m_mmprojUseGpu = params.mmproj_use_gpu;
  qDebug() << "[LocalBackend] Loading on GPU with fitted params:"
           << "n_gpu_layers=" << params.n_gpu_layers
           << "n_ctx=" << params.n_ctx << "n_ubatch=" << params.n_ubatch;

  InferenceEngine::logGpuMemory(QStringLiteral("before model load"),
                                /*setBaseline=*/true);
  common_init_result_ptr result = createInitResult(params);
  if (!result) {
    qCritical() << "[LocalBackend] GPU load failed, trying CPU fallback";
    return loadOnCpu(VramFit::fitParams(probe, {}, mmprojPath, desiredCtx,
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
  static QHash<QString, VramFit::ProbeInfo> cache;
  const QString mmprojPath = resolveMmprojPath(modelPath);
  VramFit::ProbeInfo probe;
  {
    const QMutexLocker locker(&cacheLock);
    const QString key = modelPath + '|' + mmprojPath;
    auto it = cache.constFind(key);
    if (it == cache.constEnd())
      it = cache.insert(key, VramFit::probeModel(modelPath, mmprojPath, nullptr));
    probe = *it;
  }
  if (!probe.n_layers)
    return est;

  const std::vector<VramFit::DeviceMem> gpuDevs = VramFit::discoverDevices();
  const uint32_t ctxLimit =
      probe.n_ctx_train > 0 ? static_cast<uint32_t>(probe.n_ctx_train) : 131072;
  constexpr uint32_t Ubatch = 8192, MinCtx = 2048;

  const VramFit::QuietScope quiet;
  // Placement for a prompt budget, mirroring loadModelWithDevices: GPU fit
  // first; a CPU-only result there means the CPU fallback (fitted on RAM).
  auto place = [&](uint32_t prompt, bool *truncated) {
    const uint32_t desired = std::min(prompt + responseHeadroom, ctxLimit);
    common_params p = VramFit::fitParams(probe, gpuDevs, mmprojPath, desired,
                                    Ubatch, MinCtx);
    Placement where;
    if (gpuDevs.empty() || p.n_gpu_layers == 0) {
      where = Placement::Cpu;
      p = VramFit::fitParams(probe, {}, mmprojPath, desired, Ubatch, MinCtx);
    } else {
      // n_gpu_layers includes the output layer (see VramFit::fitParams).
      where = p.n_gpu_layers > probe.n_layers ? Placement::Gpu
                                              : Placement::Split;
    }
    // The fitter shrinks n_ctx as a last resort; the prompt is then cut.
    const auto nCtx = static_cast<uint32_t>(std::max(p.n_ctx, 0));
    *truncated = nCtx < desired && VramFit::pad256(desired) > nCtx;
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
  const std::vector<PageImage> allImages = LocalPrompt::collectImages(payload);
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
  InferenceEngine::logGpuMemory(QStringLiteral("after mmproj load"));
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
  // Shared with token-counting (VramFit::probeModel) so the prompt fed to
  // inference is the one that was counted when sizing the KV cache.
  const QString userPrompt = LocalPrompt::buildUserPrompt(payload, marker, maxChars);
  return applyChatTemplate(LocalPrompt::systemPrompt(payload, !marker.isEmpty()),
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
    prompt = LocalPrompt::applyChatTemplate(m_chatTemplates.get(), systemPrompt,
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
