#include "headers/localbackend.h"

#include "headers/inferenceengine.h"

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
#include <QThread>
#include <algorithm>
#include <cstring>
#include <vector>

/// Static flag — ensures ggml + llama backends are initialised exactly once
/// across the lifetime of the process, even if multiple backend instances
/// are created.
static bool s_llamaInitialized = false;

LocalBackend::LocalBackend(QObject *parent) : QObject(parent) {
  if (!s_llamaInitialized) {
    ggml_backend_load_all(); // auto-detect CUDA, Metal, Vulkan, etc.
    llama_backend_init();    // start thread pools / memory allocators
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

// ── Model / mmproj loading ─────────────────────────────────────────

/// Load a GGUF model and (optionally) detect a multimodal projector.
///
/// Strategy: we ask libcommon to do the heavy lifting (device probing, VRAM
/// fitting, layer offload).  The mmproj is deliberately **not** loaded here —
/// it stays off the GPU until the first multimodal inference call.  This
/// prevents the projector from competing with the model for VRAM during the
/// initial load, which is already the most memory-pressured moment.
std::expected<void, QString> LocalBackend::loadModel(const QString &modelPath) {
  unloadModel();

  QString path = resolveModelPath(modelPath);
  if (path.isEmpty() || !QFile::exists(path)) {
    qCritical() << "[LocalBackend] Model file not found:" << path;
    return std::unexpected{"Model file not found: " + path};
  }

  emit statusUpdated("Loading model: " + path);

  // ── Detect mmproj (lazy-loaded on first multimodal use) ──────────
  m_mmprojPath = resolveMmprojPath(path);
  m_hasVision = !m_mmprojPath.isEmpty();

  // ── Detect devices (GPU + CPU for hybrid offloading) ─────────────
  auto devices = detectDevices();
  if (devices.empty()) {
    qCritical() << "[LocalBackend] No backend devices found";
    return std::unexpected{"No backend devices available"};
  }

  // Identify primary device for logging
  const char *devName = "";
  {
    struct ggml_backend_dev_props props;
    ggml_backend_dev_get_props(devices[0], &props);
    devName = props.name ? props.name : "unknown";
  }

  common_init_result_ptr result =
      loadModelWithDevices(path, m_mmprojPath, devices, devName);

  if (!result)
    return std::unexpected{"Failed to load model: " + QString(path)};

  m_llamaModel = result->model();

  // Steal pointers from the result — the common_init_result owns them,
  // but we need direct pointers for the inference engine.
  m_llamaCtx = result->context();

  // Store the init_result so it keeps the model+context alive.
  m_commonInit = std::move(result);

  // Cache chat templates — parsed once from GGUF metadata.
  // The model may define a Jinja-based template (most modern models do) or
  // fall back to a built-in named template (e.g. "llama-2", "chatml").
  // The release() call transfers ownership from the unique_ptr to us.
  m_chatTemplates = common_chat_templates_init(m_llamaModel, "").release();

  m_modelPath = path;
  m_ready = true;

  qDebug() << "[LocalBackend] Model loaded | ctx:" << llama_n_ctx(m_llamaCtx)
           << "| batch:" << llama_n_batch(m_llamaCtx)
           << "| ubatch:" << llama_n_ubatch(m_llamaCtx)
           << "| vision:" << m_hasVision;
  emit statusUpdated("Model loaded: " + path);
  return {};
}

// ── Adaptive memory fitting helpers ──────────────────────────────
// Strategy inspired by llama.cpp's load-balancer library: probe the
// model without allocating, estimate per-device memory needs (weights
// + KV cache + compute buffer + mmproj), then iteratively adjust
// n_gpu_layers, n_ctx, KV cache type, and n_ubatch until the config
// fits within the bottleneck GPU's free memory.
// This replaces the "n_gpu_layers=999 + brute force fallback" approach.

/// Size in bytes of one element of a ggml type.
static int lb_type_size(int type) {
  switch (type) {
  case GGML_TYPE_F32:
    return 4;
  case GGML_TYPE_F16:
  case GGML_TYPE_BF16:
    return 2;
  case GGML_TYPE_Q4_0:
  case GGML_TYPE_Q4_1:
  case GGML_TYPE_Q5_0:
  case GGML_TYPE_Q5_1:
  case GGML_TYPE_Q8_0:
  case GGML_TYPE_Q8_1:
  case GGML_TYPE_Q2_K:
  case GGML_TYPE_Q3_K:
  case GGML_TYPE_Q4_K:
  case GGML_TYPE_Q5_K:
  case GGML_TYPE_Q6_K:
  case GGML_TYPE_Q8_K:
  case GGML_TYPE_IQ2_XXS:
  case GGML_TYPE_IQ2_XS:
  case GGML_TYPE_IQ3_XXS:
  case GGML_TYPE_IQ1_S:
  case GGML_TYPE_IQ4_NL:
  case GGML_TYPE_IQ3_S:
  case GGML_TYPE_IQ2_S:
  case GGML_TYPE_IQ4_XS:
  case GGML_TYPE_IQ1_M:
    return 1;
  default:
    return 4;
  }
}

/// Block alignment in bytes for a ggml type.
static int lb_type_align(int type) {
  switch (type) {
  case GGML_TYPE_F16:
  case GGML_TYPE_BF16:
    return 2;
  case GGML_TYPE_Q4_0:
  case GGML_TYPE_Q4_1:
  case GGML_TYPE_Q5_0:
  case GGML_TYPE_Q5_1:
  case GGML_TYPE_Q8_0:
  case GGML_TYPE_Q8_1:
    return 32;
  case GGML_TYPE_Q2_K:
  case GGML_TYPE_Q3_K:
  case GGML_TYPE_Q4_K:
  case GGML_TYPE_Q5_K:
  case GGML_TYPE_Q6_K:
  case GGML_TYPE_Q8_K:
  case GGML_TYPE_IQ2_XXS:
  case GGML_TYPE_IQ2_XS:
  case GGML_TYPE_IQ3_XXS:
  case GGML_TYPE_IQ1_S:
  case GGML_TYPE_IQ4_NL:
  case GGML_TYPE_IQ3_S:
  case GGML_TYPE_IQ2_S:
  case GGML_TYPE_IQ4_XS:
  case GGML_TYPE_IQ1_M:
    return 256;
  default:
    return 1;
  }
}

/// Allocated row size for a given ggml type and element count.
/// GGML_PAD(ne * type_size, type_align).
static uint64_t lb_row_size(int type, int64_t ne) {
  int sz = lb_type_size(type);
  int al = lb_type_align(type);
  return (((static_cast<uint64_t>(ne) * sz) + al - 1) / al) * al;
}

/// Round up to nearest multiple of 256 (llama.cpp pads n_ctx to 256).
static uint32_t lb_pad256(uint32_t v) { return (v + 255U) & ~255U; }

/// Estimate KV cache size in bytes for the given configuration.
static uint64_t lb_estimate_kv_cache(int32_t n_embd_k_gqa, int32_t n_embd_v_gqa,
                                     int32_t n_layers, uint32_t n_ctx,
                                     int type_k, int type_v) {
  uint32_t padded_ctx = lb_pad256(n_ctx);
  uint64_t k_bytes = lb_row_size(type_k, n_embd_k_gqa) * padded_ctx;
  uint64_t v_bytes = lb_row_size(type_v, n_embd_v_gqa) * padded_ctx;
  return (k_bytes + v_bytes) * std::max(n_layers, 1);
}

/// Probe a GGUF model without allocating GPU memory, returning the
/// structural info needed for memory estimation.
struct probe_info {
  int32_t n_layers = 0;
  int32_t n_embd = 0;
  int32_t n_embd_k_gqa = 0;
  int32_t n_embd_v_gqa = 0;
  uint64_t model_size = 0;
  uint64_t mmproj_size = 0;
  bool has_mmproj = false;
};

/// Probe model metadata (no GPU allocation) and optionally measure mmproj.
/// Uses no_alloc=true with mmap disabled to avoid the GGML_ASSERT in
/// llama-model.cpp:1534 which fires when both are enabled simultaneously.
static probe_info lb_probe_model(const QString &modelPath,
                                 const QString &mmprojPath) {
  probe_info info{};

  auto mparams = llama_model_default_params();
  mparams.no_alloc = true;
  mparams.use_mmap = false; // required: mmap + no_alloc triggers GGML_ASSERT

  auto *model =
      llama_model_load_from_file(modelPath.toUtf8().constData(), mparams);
  if (!model) {
    qCritical() << "[LocalBackend] lb_probe: cannot load model for probing";
    return info;
  }

  info.n_layers = llama_model_n_layer(model);
  info.n_embd = llama_model_n_embd(model);
  {
    int n_head = std::max(llama_model_n_head(model), 1);
    int n_head_kv = std::max(llama_model_n_head_kv(model), 1);
    info.n_embd_k_gqa = info.n_embd / n_head * n_head_kv;
    info.n_embd_v_gqa = info.n_embd_k_gqa;
  }
  info.model_size = llama_model_size(model);
  llama_model_free(model);

  // Measure mmproj size.  The mmproj uses a "clip" architecture which
  // llama_model_load_from_file rejects.  Fallback to filesystem size.
  if (!mmprojPath.isEmpty()) {
    auto mmparams = llama_model_default_params();
    mmparams.no_alloc = true;
    mmparams.use_mmap = false;
    auto *mm =
        llama_model_load_from_file(mmprojPath.toUtf8().constData(), mmparams);
    if (mm) {
      info.mmproj_size = llama_model_size(mm);
      info.has_mmproj = true;
      llama_model_free(mm);
    } else {
      // Fallback: use file size as upper bound for weight memory.
      QFileInfo fi(mmprojPath);
      if (fi.exists()) {
        info.mmproj_size = static_cast<uint64_t>(fi.size());
        info.has_mmproj = true;
        qDebug() << "[LocalBackend] lb_probe: mmproj via file size:"
                 << (info.mmproj_size >> 20) << "MiB";
      }
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

/// Heuristic: detect integrated GPUs from the Vulkan device name/description.
/// Vulkan reports iGPUs as GGML_BACKEND_DEVICE_TYPE_OTHER (not IGPU), so we
/// must guess from the string.  Common patterns: "Radeon Graphics" (AMD APU),
/// "Intel UHD/Iris/Xe", "AMD Radeon Graphics" (no discrete model number like
/// "RX 6700" or "RX 7800").
static bool lb_is_integrated_gpu(const QString &name,
                                 const QString &description) {
  QString n = name.toLower();
  QString d = description.toLower();
  QString combined = n + " " + d;

  // Intel integrated
  if (n.contains("intel") || n.contains("uhd") || n.contains("iris"))
    return true;

  // AMD: "Radeon Graphics" (APU/iGPU) vs "RX 6xxx/7xxx" (discrete)
  if ((n.contains("radeon graphics") || combined.contains("radon graphics")) &&
      !n.contains("rx "))
    return true;

  // NVIDIA: integrated Optimus / mobile entry-level
  if (n.contains("nvs") || n.contains("geforce mx"))
    return true;

  return false;
}

/// Discover devices with free memory, returning {gpu_devs, cpu_dev}.
static void lb_discover(std::vector<device_mem> &gpuDevs, device_mem *&cpuDev) {
  cpuDev = nullptr;

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
    dm.is_integrated = props.type == GGML_BACKEND_DEVICE_TYPE_IGPU ||
                       lb_is_integrated_gpu(devName, devDesc);

    if (gpu) {
      gpuDevs.push_back(dm);
    } else if (dm.is_integrated ||
               devName.contains("Vulkan", Qt::CaseInsensitive) ||
               devName.contains("RADV", Qt::CaseInsensitive)) {
      // Vulkan on Linux reports type OTHER for GPUs — include by name
      gpuDevs.push_back(dm);
    } else if (!cpuDev) {
      cpuDev = new device_mem(std::move(dm));
    }
  }
}

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

  // No GPU available → CPU only
  if (gpuDevs.empty()) {
    params.n_gpu_layers = 0;
    qDebug() << "[LocalBackend] lb_fit: no GPU, CPU only";
    return params;
  }

  // Find bottleneck GPU (least free memory)
  auto *bottleneck = &gpuDevs[0];
  for (auto &dm : gpuDevs) {
    if (dm.memory_free < bottleneck->memory_free) {
      bottleneck = &dm;
    }
  }

  // 512 MiB safety margin for iGPUs, 1 GiB for discrete GPUs.
  bool integrated = bottleneck->is_integrated;
  uint64_t effective_margin = integrated
                                  ? 512UL * 1024 * 1024  // 512 MiB for iGPU
                                  : 512UL * 1024 * 1024; // 512 MiB for discrete
  if (bottleneck->memory_free <= effective_margin) {
    params.n_gpu_layers = 0;
    qWarning() << "[LocalBackend] lb_fit: GPU" << bottleneck->name
               << "has insufficient free memory, CPU only";
    return params;
  }
  uint64_t available = bottleneck->memory_free - effective_margin;
  if (integrated) {
    qDebug() << "[LocalBackend] lb_fit: integrated GPU detected ("
             << bottleneck->name << "), margin=" << (effective_margin >> 20)
             << "MiB, available=" << (available >> 20) << "MiB";
  }

  // Per-layer weight estimate
  uint64_t per_layer = probe.model_size / std::max(probe.n_layers, 1);
  uint64_t non_layer = probe.model_size - per_layer * probe.n_layers;

  // --- Integrated GPU: all-or-nothing strategy ---
  // Splitting layers between iGPU and CPU triggers
  // GGML_SCHED_MAX_SPLIT_INPUTS overflow in the Vulkan scheduler.
  // Strategy: use n_gpu_layers=999 (proven to work) with full context.
  // We only check that model weights + KV cache fit (the persistent GPU
  // allocations).  The mmproj is lazy-loaded later (excluded from estimate).
  // The compute buffer is transient — freed between decode calls (excluded).
  if (integrated) {
    params.n_gpu_layers = 999;
    params.n_ctx = desiredCtx;
    params.n_batch = desiredCtx;
    params.n_ubatch = desiredUbatch;
    params.devices = {bottleneck->dev};

    // Persistent GPU estimate: model weights + KV cache
    uint64_t kv_size = lb_estimate_kv_cache(
        probe.n_embd_k_gqa, probe.n_embd_v_gqa, probe.n_layers, desiredCtx,
        GGML_TYPE_F16, GGML_TYPE_F16);
    uint64_t persistent = probe.model_size + kv_size;

    if (persistent <= available) {
      // Fits with full context — done.
      if (probe.has_mmproj) {
        params.mmproj.path = mmprojPath.toStdString();
        params.mmproj_use_gpu = false; // lazy-loaded, not counted
      }
      qDebug() << "[LocalBackend] lb_fit: iGPU OK (full params)"
               << "| persistent=" << (persistent >> 20) << "MiB /"
               << (available >> 20) << "MiB"
               << "| model=" << (probe.model_size >> 20) << "MiB"
               << "| kv=" << (kv_size >> 20) << "MiB"
               << "| n_ctx=" << desiredCtx;
      return params;
    }

    // Doesn't fit — try reducing only n_ctx (weights are fixed).
    // Floor at 4096 — below that multimodal inference is useless.
    uint32_t fit_ctx = lb_pad256(desiredCtx);
    uint32_t floor_ctx = std::max(minCtx, 4096U);

    while (fit_ctx > lb_pad256(floor_ctx)) {
      uint64_t fit_kv = lb_estimate_kv_cache(
          probe.n_embd_k_gqa, probe.n_embd_v_gqa, probe.n_layers, fit_ctx,
          GGML_TYPE_F16, GGML_TYPE_F16);
      if (probe.model_size + fit_kv <= available) {
        params.n_ctx = fit_ctx;
        params.n_batch = fit_ctx;
        if (probe.has_mmproj) {
          params.mmproj.path = mmprojPath.toStdString();
          params.mmproj_use_gpu = false;
        }
        qDebug() << "[LocalBackend] lb_fit: iGPU OK (reduced ctx)"
                 << "| n_ctx=" << fit_ctx
                 << "| persistent=" << ((probe.model_size + fit_kv) >> 20)
                 << "MiB /" << (available >> 20) << "MiB";
        return params;
      }
      fit_ctx = lb_pad256(fit_ctx - 1024U);
    }

    // Weights alone don't fit → CPU fallback
    qWarning() << "[LocalBackend] lb_fit: iGPU cannot fit model ("
               << (probe.model_size >> 20) << "MiB model vs"
               << (available >> 20) << "MiB available), CPU fallback";
    params.n_gpu_layers = 0;
    return params;
  }

  // --- Discrete GPU path: progressive layer offload ---
  // Strategy (priority order — cheapest first):
  //   1. n_ubatch /2 (halves compute buffer, ~4 Go saved)
  //   2. KV cache F16 → Q8_0 (halves KV cache, ~2 Go saved)
  //   3. mmproj → CPU (free ~167 MiB)
  //   4. Offload 1 layer to CPU at a time
  //   5. After 30 % offload: n_ctx -10 %, KV Q8→Q4
  // This ensures the _minimum_ number of layers ends up on CPU.
  (void)non_layer;
  int total_layers = probe.n_layers;
  int offloaded = 0; // layers moved to CPU so far
  uint32_t n_ctx = lb_pad256(desiredCtx);
  uint32_t n_ubatch = desiredUbatch;
  int type_k = GGML_TYPE_F16;
  int type_v = GGML_TYPE_F16;
  bool mmproj_gpu = probe.has_mmproj;
  bool kv_quantized = false;
  bool mmproj_moved = false;
  bool post_30 = false; // enters here after 30% offload

  const int max_attempts = 200;
  int attempts = 0;

  while (attempts++ < max_attempts) {
    int n_gpu_layers = total_layers - offloaded;
    // Estimate memory demand (GPU-side only — CPU layers cost nothing here)
    // Only GPU layers store their KV cache on GPU; CPU layers use system RAM.
    int kv_layers = n_gpu_layers > 0 ? n_gpu_layers : probe.n_layers;
    uint64_t kv_size =
        lb_estimate_kv_cache(probe.n_embd_k_gqa, probe.n_embd_v_gqa, kv_layers,
                             n_ctx, type_k, type_v);
    uint64_t gpu_weight =
        n_gpu_layers > 0 ? static_cast<uint64_t>(n_gpu_layers) * per_layer : 0;
    uint64_t mmproj_mem =
        (probe.has_mmproj && mmproj_gpu) ? probe.mmproj_size : 0;
    uint64_t compute_buf =
        n_gpu_layers > 0
            // CUDA graph reserve allocates per-layer activations.
            // Real measurement: 8433 MiB for 48 layers, 8192 ubatch, 3840 embd.
            // Formula: n_ubatch * n_embd * n_layers * 8 / 1e6 ≈ 8024 MiB.
            ? static_cast<uint64_t>(n_ubatch) * std::max(probe.n_embd, 1) *
                  static_cast<uint64_t>(n_gpu_layers) * 8
            : 0;
    uint64_t gpu_total =
        (gpu_weight + mmproj_mem + kv_size + compute_buf) * 11 / 10;

    if (gpu_total <= available) {
      // Fits!
      params.n_gpu_layers = n_gpu_layers;
      params.n_batch = n_ctx;
      params.n_ubatch = n_ubatch;
      params.devices = {bottleneck->dev};
      params.cache_type_k = static_cast<ggml_type>(type_k);
      params.cache_type_v = static_cast<ggml_type>(type_v);
      // llama.cpp uses n_gpu_layers to decide how many layers go to the first
      // device (GPU); the remaining layers are automatically placed on the
      // CPU backend — no need to pass the CPU device explicitly here.
      if (n_gpu_layers < total_layers && n_gpu_layers > 0) {
        qDebug() << "[LocalBackend] lb_fit: OK (split GPU/CPU)"
                 << "| n_gpu_layers=" << n_gpu_layers
                 << "| n_cpu_layers=" << offloaded << "| n_ctx=" << n_ctx
                 << "| n_ubatch=" << n_ubatch
                 << "| kv_type=" << (type_k == GGML_TYPE_F16 ? "F16" : "Q8_0")
                 << "| GPU" << (gpu_total >> 20) << "MiB /" << (available >> 20)
                 << "MiB"
                 << "| weights=" << (gpu_weight >> 20) << "MiB"
                 << "| kv=" << (kv_size >> 20) << "MiB"
                 << "| mmproj=" << (mmproj_mem >> 20) << "MiB"
                 << "| compute=" << (compute_buf >> 20) << "MiB";
      } else if (n_gpu_layers <= 0) {
        params.n_gpu_layers = 0;
        qDebug() << "[LocalBackend] lb_fit: OK (CPU only)"
                 << "| n_ctx=" << n_ctx << "| n_ubatch=" << n_ubatch;
      } else {
        qDebug() << "[LocalBackend] lb_fit: OK (GPU only)"
                 << "| n_gpu_layers=" << n_gpu_layers << "| n_ctx=" << n_ctx
                 << "| n_ubatch=" << n_ubatch
                 << "| kv_type=" << (type_k == GGML_TYPE_F16 ? "F16" : "Q8_0")
                 << "| GPU" << (gpu_total >> 20) << "MiB /" << (available >> 20)
                 << "MiB"
                 << "| weights=" << (gpu_weight >> 20) << "MiB"
                 << "| kv=" << (kv_size >> 20) << "MiB"
                 << "| mmproj=" << (mmproj_mem >> 20) << "MiB"
                 << "| compute=" << (compute_buf >> 20) << "MiB";
      }
      if (probe.has_mmproj) {
        params.mmproj.path = mmprojPath.toStdString();
        params.mmproj_use_gpu = mmproj_gpu;
      }
      return params;
    }

    // ── OOM: progressive reduction (cheapest first) ─────────────

    // 1. n_ubatch /2 (halves compute buffer, biggest single win)
    if (n_ubatch > 2048) {
      uint32_t old = n_ubatch;
      n_ubatch = std::max(n_ubatch / 2, 2048U);
      qDebug() << "[LocalBackend] lb_fit: OOM → n_ubatch" << old << "→"
               << n_ubatch;
      continue;
    }

    // 2. KV cache F16 → Q8_0 (halves KV cache, ~2 Go saved)
    if (!kv_quantized && type_k == GGML_TYPE_F16) {
      kv_quantized = true;
      type_k = type_v = GGML_TYPE_Q8_0;
      qDebug() << "[LocalBackend] lb_fit: OOM → KV cache F16 → Q8_0";
      continue;
    }

    // 2. mmproj → CPU (~167 MiB freed)
    if (!mmproj_moved && probe.has_mmproj && mmproj_gpu) {
      mmproj_moved = true;
      mmproj_gpu = false;
      qDebug() << "[LocalBackend] lb_fit: OOM → mmproj → CPU ("
               << (probe.mmproj_size >> 20) << "MiB freed)";
      continue;
    }

    // 3. Offload 1 layer to CPU at a time (granular, preserves GPU performance)
    {
      int next = offloaded + 1;
      if (next <= total_layers) {
        offloaded = next;
        qDebug() << "[LocalBackend] lb_fit: OOM → offload 1 more layer to CPU"
                 << "(total offloaded=" << offloaded << "/" << total_layers
                 << ")";
        // After 30 % offload, unlock the more aggressive parameter tweaks.
        if (offloaded >= (total_layers * 30 + 9) / 10) {
          post_30 = true;
        }
        continue;
      }
    }

    // 4. After 30 % offload: touch other params
    if (!post_30)
      break; // nothing left to try

    // 4a. Reduce n_ctx by 10 %
    if (n_ctx > lb_pad256(minCtx)) {
      uint32_t old = n_ctx;
      n_ctx = lb_pad256(std::max(n_ctx - n_ctx / 10, lb_pad256(minCtx)));
      if (n_ctx == old)
        n_ctx = lb_pad256(minCtx);
      qDebug() << "[LocalBackend] lb_fit: OOM → n_ctx" << old << "→" << n_ctx;
      continue;
    }

    // 4b. Quantize KV cache: Q8_0 → Q4_K (last resort)
    if (type_k == GGML_TYPE_Q8_0) {
      type_k = type_v = GGML_TYPE_Q4_K;
      qDebug() << "[LocalBackend] lb_fit: OOM → KV cache Q8_0 → Q4_K";
      continue;
    }

    // 4c. Reduce n_ubatch by half (floor 64)
    if (n_ubatch > 64) {
      n_ubatch = std::max(n_ubatch / 2, 64U);
      qDebug() << "[LocalBackend] lb_fit: OOM → n_ubatch" << n_ubatch;
      continue;
    }

    // 4d. Emergency: reduce n_ctx below minCtx
    if (n_ctx > 256) {
      uint32_t old = n_ctx;
      n_ctx = lb_pad256(std::max(n_ctx - 256U, 256U));
      if (n_ctx == old)
        n_ctx = 256;
      qDebug() << "[LocalBackend] lb_fit: OOM → emergency n_ctx" << n_ctx;
      continue;
    }

    // Nothing left — give up
    qWarning() << "[LocalBackend] lb_fit: cannot fit on GPU,"
               << (gpu_total >> 20) << "MiB needed vs" << (available >> 20)
               << "MiB available";
    break;
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

/// Build a common_init_result, handling hybrid model flash-attention
/// re-init.  On failure returns nullptr and logs the error.
common_init_result_ptr LocalBackend::createInitResult(common_params &params,
                                                      bool hasMmprojOnGpu) {
  auto result = common_init_from_params(params);
  if (!result)
    return nullptr;
  if (!result->model() || !result->context())
    return nullptr;

  // Hybrid models (Siglip + LLaMA) crash with flash attention because the
  // vision encoder writes embeddings at unexpected positions.
  if (llama_model_is_hybrid(result->model())) {
    result.reset();
    params.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_AUTO;
    qDebug() << "[LocalBackend] Flash attention set to AUTO (hybrid model)";
    result = common_init_from_params(params);
    if (!result || !result->model() || !result->context())
      return nullptr;
  }
  return result;
}

/// Model loader: adaptive memory fitting via lb strategy.
///
/// Replaces the old "n_gpu_layers=999 + brute force fallback" with a
/// three-phase approach:
///   1. Probe the model (no GPU allocation) to get layers, embd, size.
///   2. Run lb_fit_params which iteratively adjusts n_gpu_layers,
///      n_ctx, KV cache type, and n_ubatch until it fits the bottleneck GPU.
///   3. If GPU fitting fails entirely, fall back to pure CPU.
///
/// The mmproj path is stored in params so common_init_from_params knows
/// about it, but the projector is lazy-loaded (not bound to GPU here).
common_init_result_ptr LocalBackend::loadModelWithDevices(
    const QString &path, const QString &mmprojPath,
    const std::vector<ggml_backend_dev_t> &devices, const char *primaryName) {
  (void)primaryName;

  // ── Phase 1: probe model metadata (no GPU allocation) ──────────
  probe_info probe = lb_probe_model(path, mmprojPath);
  if (!probe.n_layers) {
    qCritical() << "[LocalBackend] Model probe failed:" << path;
    return nullptr;
  }

  // ── Phase 2: discover devices and run adaptive fit ─────────────
  std::vector<device_mem> gpuDevs;
  device_mem *cpuDev = nullptr;
  lb_discover(gpuDevs, cpuDev);

  uint32_t desiredCtx = MaxPromptTokens + MaxGenTokens;
  uint32_t desiredUbatch = 8192;
  uint32_t minCtx = 2048;

  common_params params = lb_fit_params(probe, gpuDevs, mmprojPath, desiredCtx,
                                       desiredUbatch, minCtx);

  // Apply remaining common settings
  params.model.path = path.toStdString();
  params.cpuparams.n_threads = QThread::idealThreadCount();
  params.cpuparams_batch.n_threads = QThread::idealThreadCount();

  if (params.n_gpu_layers > 0) {
    // GPU path: the fitter already set params.devices to {bottleneck_gpu}
    params.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_AUTO;
    if (!mmprojPath.isEmpty() && !probe.has_mmproj) {
      // lb_fit_params only sets mmproj if probe confirmed it exists
      params.mmproj.path = mmprojPath.toStdString();
    }
    qDebug() << "[LocalBackend] Loading on GPU with fitted params:"
             << "n_gpu_layers=" << params.n_gpu_layers
             << "n_ctx=" << params.n_ctx << "n_ubatch=" << params.n_ubatch;
  } else {
    // ── Phase 3: CPU fallback ────────────────────────────────────
    if (cpuDev) {
      params.devices = {cpuDev->dev};
    } else {
      // Last resort: use whatever device list detectDevices gave us
      if (!devices.empty()) {
        params.devices = {devices[0]};
      }
    }
    params.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_DISABLED;
    if (!mmprojPath.isEmpty()) {
      params.mmproj.path = mmprojPath.toStdString();
      params.mmproj_use_gpu = false;
    }
    qDebug() << "[LocalBackend] Loading on CPU (GPU fit failed)"
             << "n_ctx=" << params.n_ctx;
    delete cpuDev;
    return createInitResult(params, false);
  }

  auto result = createInitResult(params, params.mmproj_use_gpu);
  if (!result) {
    qCritical() << "[LocalBackend] Fitted load failed, trying CPU fallback";
    params =
        lb_fit_params(probe, {}, mmprojPath, desiredCtx, desiredUbatch, minCtx);
    params.model.path = path.toStdString();
    params.cpuparams.n_threads = QThread::idealThreadCount();
    params.cpuparams_batch.n_threads = QThread::idealThreadCount();
    delete cpuDev;
    if (params.devices.empty()) {
      params.devices = devices.empty() ? devices : std::vector{devices[0]};
    }
    params.n_gpu_layers = 0;
    params.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_DISABLED;
    if (!mmprojPath.isEmpty()) {
      params.mmproj.path = mmprojPath.toStdString();
      params.mmproj_use_gpu = false;
    }
    if (params.devices.empty())
      return nullptr;
    return createInitResult(params, false);
  }

  delete cpuDev;

  qDebug() << "[LocalBackend] Model loaded | ctx:"
           << llama_n_ctx(result->context())
           << "| batch:" << llama_n_batch(result->context())
           << "| ubatch:" << llama_n_ubatch(result->context());
  return result;
}

void LocalBackend::unloadModel() {
  unloadMmproj();
  if (m_chatTemplates) {
    common_chat_templates_free(m_chatTemplates);
    m_chatTemplates = nullptr;
  }
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

/// Main public entry point. Computes how much prompt space is available
/// (leaving room for generation), truncates the text context accordingly,
/// and dispatches to text-only or multimodal inference.
QString LocalBackend::summarize(const PromptPayload &payload) {
  if (!m_ready) {
    qWarning() << "[LocalBackend] Model not loaded";
    return QString();
  }

  // Calculate available prompt budget: the KV cache must hold both the prompt
  // and the generated output.  We reserve MaxGenTokens for the response, so
  // the prompt can use the remainder.  The 3-char heuristic is a conservative
  // upper bound on average token length (accounts for multi-byte UTF-8).
  // If n_ctx is smaller than MaxGenTokens (unlikely), clamp gen budget.
  int32_t ctxSize = llama_n_ctx(m_llamaCtx);
  int32_t genBudget = std::min(MaxGenTokens, ctxSize / 2);
  int32_t maxPromptToks = std::max(1, ctxSize - genBudget);
  int maxChars = maxPromptToks * 3;

  qDebug() << "[LocalBackend] summarize() — ctxSize:" << ctxSize
           << "| maxPromptToks:" << maxPromptToks << "| maxChars:" << maxChars
           << "| textContext:" << payload.textContext.length() << "chars"
           << "| visuals:" << payload.visualContext.size();

  QString promptText = buildFormattedPrompt(payload, maxChars);

  // Decide inference path: multimodal only when vision is available, the
  // mmproj is currently loaded, and there are actual images to process.
  // Note: m_mtmdCtx check is intentional here — if the mmproj hasn't been
  // loaded yet we fall through to text-only.  The caller can retry after
  // an explicit loadMmproj() call if needed.
  if (m_hasVision && m_mtmdCtx && !payload.visualContext.empty()) {
    return runMultimodalInference(promptText, payload.visualContext);
  }
  return runTextInference(promptText);
}

QString LocalBackend::generate(const QString &systemPrompt,
                               const QString &userPrompt) {
  PromptPayload p;
  p.systemPrompt = systemPrompt;
  p.userMessage = userPrompt;
  p.textContext = userPrompt;
  return summarize(p);
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

QString LocalBackend::runTextInference(const QString &formattedText) {
  InferenceEngine engine(m_llamaCtx, m_llamaModel, makeSamplingParams(),
                         MaxGenTokens);
  QString result = engine.inferText(formattedText);

  if (result.isEmpty()) {
    qWarning() << "[LocalBackend] Text inference returned empty output";
  }
  return result;
}

// ── Multimodal inference ──────────────────────────────────────────

/// Run inference with images. The mmproj is lazy-loaded before the call
/// and unloaded immediately after — this keeps VRAM pressure low between
/// text-only and multimodal workloads.
QString
LocalBackend::runMultimodalInference(const QString &formattedText,
                                     const std::vector<PageImage> &images) {
  if (!m_hasVision) {
    qWarning() << "[LocalBackend] Multimodal inference requested but "
                  "vision not available";
    return QString();
  }

  // Lazy-load mmproj on first use (or if previously unloaded).
  auto r = loadMmproj();
  if (!r) {
    qWarning() << "[LocalBackend] Failed to lazy-load mmproj:" << r.error();
    return QString();
  }

  qDebug() << "[LocalBackend] Multimodal inference with" << images.size()
           << "images";

  InferenceEngine engine(m_llamaCtx, m_llamaModel, makeSamplingParams(),
                         MaxGenTokens);
  QString result = engine.inferMultimodal(formattedText, images, m_mtmdCtx);

  // Unload mmproj to free GPU VRAM after inference.
  // This is critical: the mmproj can take 100-300 MiB of VRAM.  Keeping it
  // around for the next text-only inference would be wasteful.
  unloadMmproj();

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

  emit statusUpdated("Loading multimodal projector: " + m_mmprojPath);

  auto mtmdParams = mtmd_context_params_default();
  mtmdParams.use_gpu = true;
  mtmdParams.n_threads = QThread::idealThreadCount();
  mtmdParams.warmup = false;

  m_mtmdCtx = mtmd_init_from_file(m_mmprojPath.toUtf8().constData(),
                                  m_llamaModel, mtmdParams);
  if (!m_mtmdCtx) {
    qWarning() << "[LocalBackend] Failed to load mmproj:" << m_mmprojPath;
    return std::unexpected{"Failed to load mmproj: " + m_mmprojPath};
  }

  qDebug() << "[LocalBackend] mmproj loaded on GPU";
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

/// Assemble the user prompt from visual markers + text context, then wrap
/// in the model's chat template.  The marker is a special token placeholder
/// (e.g. "<__media__>") that mtmd_tokenize replaces with image embeddings.
QString LocalBackend::buildFormattedPrompt(const PromptPayload &payload,
                                           int32_t maxChars) {
  const char *rawMarker =
      m_mtmdCtx != nullptr ? mtmd_get_marker(m_mtmdCtx) : nullptr;
  const char *marker = rawMarker ? rawMarker : "<__media__>";

  QString userPrompt;
  if (!payload.visualContext.empty()) {
    userPrompt += "## VISUAL CONTEXT\n";
    for (size_t i = 0; i < payload.visualContext.size(); ++i) {
      userPrompt += QString("Page %1: %2\n").arg(i + 1).arg(marker);
    }
    userPrompt += "\n";
  }
  userPrompt += payload.userMessage + "\n\n";
  userPrompt += "## TEXT CONTEXT\n\n";

  QString ctx = payload.textContext;
  bool truncated = ctx.length() > maxChars;
  if (truncated) {
    ctx = ctx.left(maxChars) + "\n\n... [truncated]";
    qDebug() << "[LocalBackend] buildFormattedPrompt: textContext truncated"
             << payload.textContext.length() << "→" << ctx.length()
             << "chars (maxChars=" << maxChars << ")";
  }
  userPrompt += ctx;

  return applyChatTemplate(payload.systemPrompt, userPrompt);
}

QString LocalBackend::applyChatTemplate(const QString &systemPrompt,
                                        const QString &userMessage) {
  if (!m_chatTemplates) {
    qWarning() << "[LocalBackend] Chat templates not initialized";
    return QString();
  }

  // Build inputs with thinking disabled — prevents <|channel>thought tags
  // from reasoning models like Gemma4 which break JSON output extraction.
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

  std::string formatted =
      common_chat_templates_apply(m_chatTemplates, inputs).prompt;

  if (formatted.empty()) {
    qWarning() << "[LocalBackend] Chat template produced empty output";
    return QString();
  }

  return QString::fromStdString(formatted);
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
QString LocalBackend::resolveMmprojPath(const QString &modelPath) const {
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
