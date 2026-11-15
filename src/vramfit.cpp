#include "vramfit.h"

#include "inferenceengine.h"

#include "common/chat.h"
#include "common/common.h"
#include "ggml-backend.h"
#include "ggml.h"
#include "gguf.h"
#include "llama.h"
#include "mtmd.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegularExpression>
#include <QThread>
#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

namespace VramFit {

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
uint32_t pad256(uint32_t v) { return (v + 255U) & ~255U; }

/// Short name of a KV cache type, for logs.
static const char *lb_kv_name(int type) {
  return ggml_type_name(static_cast<ggml_type>(type));
}

/// Exact KV cache size, in bytes, of the LAST `n_layers_on_dev` layers
/// (llama.cpp offloads the last n_gpu_layers layers to the GPU; pass the
/// layer count for the whole model).  Full-attention layers hold n_ctx
/// cells, sliding-window ones min(n_ctx, n_swa + n_ubatch) (llama.cpp's
/// non-"swa_full" layout), both padded to 256.
static uint64_t lb_estimate_kv_cache(const std::vector<KvLayer> &layers,
                                     uint32_t n_swa, int n_layers_on_dev,
                                     uint32_t n_ctx, uint32_t n_ubatch,
                                     int type_k, int type_v) {
  const uint32_t cells_full = pad256(n_ctx);
  const uint32_t cells_swa =
      std::min(cells_full, pad256(n_swa + std::max(n_ubatch, 1U)));
  const int n = static_cast<int>(layers.size());
  const int first = std::clamp(n - n_layers_on_dev, 0, n);
  uint64_t total = 0;
  for (int il = first; il < n; ++il) {
    const KvLayer &l = layers[il];
    if (l.k_dim == 0 && l.v_dim == 0)
      continue;
    const uint64_t cells = l.swa ? cells_swa : cells_full;
    total += (lb_row_size(type_k, l.k_dim) + lb_row_size(type_v, l.v_dim)) *
             cells;
  }
  return total;
}

/// V cache type actually used for a requested KV type: llama.cpp rejects a
/// quantized V cache without flash attention, so it stays F16 then (see
/// LocalBackend::createInitResult).  A quantized K cache is always allowed.
static int lb_v_type(int type_kv, bool flash_attn) {
  return !flash_attn && ggml_is_quantized(static_cast<ggml_type>(type_kv))
             ? GGML_TYPE_F16
             : type_kv;
}

/// Fallback tokens per image when the probe cannot count them exactly
/// (projector failed to load on the CPU).  The real count depends on the
/// model and image size; for a page capped at PageImage::kModelMaxPixels:
/// Qwen3.5 ≈ 870, Gemma 4 ≈ 390.  Capped at the projector's maximum when it
/// reports one.  If the real total is higher, summarize() counts exactly and
/// truncates the last pages.
static constexpr int32_t kImageTokenEstimate = 800;

/* ── KV layout from the GGUF metadata ──
   llama_model_meta_val_str() does not return arrays (per-layer head counts,
   sliding-window patterns), so the header is read with the gguf API (no
   tensor data).  Keys follow llama.cpp's naming: "<arch>.<key>". */

namespace {
struct GgufReader {
  gguf_context *ctx = nullptr;
  std::string arch;
  explicit GgufReader(const QString &path) {
    gguf_init_params params{/*no_alloc=*/true, /*ctx=*/nullptr};
    ctx = gguf_init_from_file(path.toUtf8().constData(), params);
    if (ctx) {
      const int64_t k = gguf_find_key(ctx, "general.architecture");
      if (k >= 0 && gguf_get_kv_type(ctx, k) == GGUF_TYPE_STRING)
        arch = gguf_get_val_str(ctx, k);
    }
  }
  ~GgufReader() {
    if (ctx)
      gguf_free(ctx);
  }
  GgufReader(const GgufReader &) = delete;
  GgufReader &operator=(const GgufReader &) = delete;

  int64_t key(const char *suffix) const {
    return ctx ? gguf_find_key(ctx, (arch + "." + suffix).c_str()) : -1;
  }
  /// Integer scalar (any integer width), or `def` if absent, an array or a
  /// string.  The type is checked first: gguf_get_val_data() asserts on
  /// arrays (e.g. Gemma 4's per-layer sliding_window_pattern).
  int64_t integer(const char *suffix, int64_t def) const {
    const int64_t k = key(suffix);
    if (k < 0)
      return def;
    const gguf_type t = gguf_get_kv_type(ctx, k);
    if (t == GGUF_TYPE_ARRAY || t == GGUF_TYPE_STRING)
      return def;
    return scalarAt(t, gguf_get_val_data(ctx, k), 0, def);
  }
  /// Per-layer integer array (bools as 0/1); empty if absent or a scalar.
  std::vector<int64_t> array(const char *suffix) const {
    std::vector<int64_t> out;
    const int64_t k = key(suffix);
    if (k < 0 || gguf_get_kv_type(ctx, k) != GGUF_TYPE_ARRAY)
      return out;
    const gguf_type t = gguf_get_arr_type(ctx, k);
    if (t == GGUF_TYPE_STRING || t == GGUF_TYPE_ARRAY)
      return out; // gguf_get_arr_data() asserts on these
    const void *data = gguf_get_arr_data(ctx, k);
    const size_t n = gguf_get_arr_n(ctx, k);
    for (size_t i = 0; i < n; ++i)
      out.push_back(scalarAt(t, data, i, 0));
    return out;
  }

private:
  static int64_t scalarAt(gguf_type t, const void *data, size_t i,
                          int64_t def) {
    switch (t) {
    case GGUF_TYPE_UINT8:
    case GGUF_TYPE_BOOL: // stored as int8
      return static_cast<const uint8_t *>(data)[i];
    case GGUF_TYPE_INT8:
      return static_cast<const int8_t *>(data)[i];
    case GGUF_TYPE_UINT16:
      return static_cast<const uint16_t *>(data)[i];
    case GGUF_TYPE_INT16:
      return static_cast<const int16_t *>(data)[i];
    case GGUF_TYPE_UINT32:
      return static_cast<const uint32_t *>(data)[i];
    case GGUF_TYPE_INT32:
      return static_cast<const int32_t *>(data)[i];
    case GGUF_TYPE_UINT64:
      return static_cast<int64_t>(static_cast<const uint64_t *>(data)[i]);
    case GGUF_TYPE_INT64:
      return static_cast<const int64_t *>(data)[i];
    default:
      return def;
    }
  }
};
} // namespace

/// Per-layer KV cache shape, as llama.cpp builds it:
///   - n_head_kv: scalar or per-layer array (0 = no KV: recurrent layer);
///   - hybrid models with "full_attention_interval" (Qwen3.5/Qwen3-Next):
///     only every interval-th layer has attention;
///   - "sliding_window_pattern": per-layer bools (true = sliding window), or
///     an integer period p (layer il is SWA unless il % p == p - 1);
///   - "shared_kv_layers": the last N layers reuse earlier layers' KV;
///   - head sizes "key_length" / "value_length" and their "_swa" variants.
/// Unknown layouts fall back to full attention on every layer with
/// n_embd / n_head sized heads: an overestimate, never an underestimate
/// (a sliding window without a pattern in the metadata counts as full).
static std::vector<KvLayer> lb_read_kv_layout(const QString &modelPath,
                                               int n_layers, int n_embd,
                                               int n_head, int n_head_kv,
                                               uint32_t n_swa) {
  const GgufReader g(modelPath);
  const int64_t head_k = g.integer("attention.key_length", n_embd / n_head);
  const int64_t head_v = g.integer("attention.value_length", head_k);
  const int64_t head_k_swa = g.integer("attention.key_length_swa", head_k);
  const int64_t head_v_swa = g.integer("attention.value_length_swa", head_v);
  const std::vector<int64_t> kv_heads = g.array("attention.head_count_kv");
  const int64_t full_interval = g.integer("full_attention_interval", 0);
  const std::vector<int64_t> swa_pattern =
      g.array("attention.sliding_window_pattern");
  const int64_t swa_period = g.integer("attention.sliding_window_pattern", 0);
  const int64_t shared = g.integer("attention.shared_kv_layers", 0);

  std::vector<KvLayer> layers(static_cast<size_t>(std::max(n_layers, 0)));
  for (int il = 0; il < n_layers; ++il) {
    int64_t heads =
        il < static_cast<int>(kv_heads.size()) ? kv_heads[il] : n_head_kv;
    if (full_interval > 0 && (il + 1) % full_interval != 0)
      heads = 0; // recurrent layer
    if (shared > 0 && il >= n_layers - shared)
      heads = 0; // reuses an earlier layer's KV
    bool swa = false;
    if (n_swa > 0) {
      if (il < static_cast<int>(swa_pattern.size()))
        swa = swa_pattern[il] != 0;
      else if (swa_period > 0)
        swa = il % swa_period < swa_period - 1;
    }
    layers[il].swa = swa;
    layers[il].k_dim =
        static_cast<uint32_t>(heads * (swa ? head_k_swa : head_k));
    layers[il].v_dim =
        static_cast<uint32_t>(heads * (swa ? head_v_swa : head_v));
  }
  return layers;
}

/// Probe model metadata (no GPU allocation) and optionally measure mmproj.
/// Uses no_alloc=true with mmap disabled to avoid the GGML_ASSERT in
/// llama-model.cpp:1534 which fires when both are enabled simultaneously.
/// When a payload is provided, the exact prompt token count is measured with
/// the same no-alloc model load (no extra load).
ProbeInfo probeModel(const QString &modelPath,
                          const QString &mmprojPath,
                          const PromptPayload *payload) {
  ProbeInfo info{};

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
    info.n_head = n_head;
    info.n_swa = static_cast<uint32_t>(std::max(llama_model_n_swa(model), 0));
    info.kv_layers = lb_read_kv_layout(modelPath, info.n_layers, info.n_embd,
                                       n_head, n_head_kv, info.n_swa);
    int full = 0, swa = 0;
    for (const KvLayer &l : info.kv_layers)
      if (l.k_dim || l.v_dim)
        ++(l.swa ? swa : full);
    qDebug() << "[LocalBackend] lb_probe: KV layout:" << full
             << "full-attention +" << swa << "sliding-window (n_swa"
             << info.n_swa << ") of" << info.n_layers << "layers";
  }
  info.model_size = llama_model_size(model);
  info.is_hybrid = llama_model_is_hybrid(model);

  // Count the exact text tokens while the no-alloc model is already open.
  // Images are only part of the prompt when a projector exists; their exact
  // cost is measured below with a temporary CPU projector.
  const bool vision = !mmprojPath.isEmpty();
  const std::vector<PageImage> images =
      vision && payload ? LocalPrompt::collectImages(*payload) : std::vector<PageImage>{};
  common_chat_prompt prompt;
  if (payload) {
    // No char cap (0) — counting needs the full prompt.
    const QString userPrompt = LocalPrompt::buildUserPrompt(
        *payload, vision ? QString::fromUtf8(mtmd_default_marker()) : QString(),
        0);
    try {
      auto tmpl = common_chat_templates_init(model, "");
      if (tmpl)
        prompt = LocalPrompt::applyChatTemplate(
            tmpl.get(), LocalPrompt::systemPrompt(*payload, !images.empty()),
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
std::vector<DeviceMem> discoverDevices() {
  std::vector<DeviceMem> gpuDevs;

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

    DeviceMem dm;
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
const DeviceMem *pickGpu(const std::vector<DeviceMem> &gpuDevs) {
  const DeviceMem *best = nullptr;
  auto key = [](const DeviceMem &d) {
    return std::tuple(!d.is_integrated,
                      d.name.startsWith("CUDA", Qt::CaseInsensitive),
                      d.memory_free);
  };
  for (const auto &dm : gpuDevs)
    if (!best || key(dm) > key(*best))
      best = &dm;
  return best;
}

/// Set by QuietScope: silences fitParams' logging while
/// LocalBackend::estimateContext runs it repeatedly.
static thread_local bool s_fitQuiet = false;

QuietScope::QuietScope() : m_previous(s_fitQuiet) { s_fitQuiet = true; }
QuietScope::~QuietScope() { s_fitQuiet = m_previous; }
#define LB_FIT_LOG(fn)                                                         \
  if (s_fitQuiet) {                                                            \
  } else                                                                       \
    fn()

/// Run the adaptive fitting loop. Returns a common_params tuned to fit
/// within the available GPU memory. On failure (no GPU or cannot fit),
/// returns params configured for pure CPU.
common_params fitParams(const ProbeInfo &probe,
                        const std::vector<DeviceMem> &gpuDevs,
                        const QString &mmprojPath, uint32_t desiredCtx,
                        uint32_t desiredUbatch, uint32_t minCtx) {
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
    uint32_t n_ctx = pad256(desiredCtx);
    uint32_t floor_ctx = pad256(std::max(minCtx, 4096U));
    int type_k = GGML_TYPE_F16;
    int type_v = GGML_TYPE_F16;

    uint64_t kv = lb_estimate_kv_cache(probe.kv_layers, probe.n_swa,
                                       probe.n_layers, n_ctx, params.n_ubatch,
                                       type_k, type_v);
    while (persistentWeights + kv > cpuAvailable) {
      // First try quantizing KV (cheap, ~a quarter of the cache for K only).
      if (type_k == GGML_TYPE_F16) {
        type_k = GGML_TYPE_Q8_0;
        type_v = lb_v_type(type_k, /*flash_attn=*/false);
        kv = lb_estimate_kv_cache(probe.kv_layers, probe.n_swa,
                                  probe.n_layers, n_ctx, params.n_ubatch,
                                  type_k, type_v);
        continue;
      }
      // Then shrink the context.
      if (n_ctx <= floor_ctx)
        break; // cannot fit even at the floor — accept the risk
      n_ctx = pad256(n_ctx - 1024U);
      kv = lb_estimate_kv_cache(probe.kv_layers, probe.n_swa, probe.n_layers,
                                n_ctx, params.n_ubatch, type_k, type_v);
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
  const DeviceMem *bottleneck = pickGpu(gpuDevs);

  // Safety margin kept free on the GPU: 512 MiB for iGPUs; for discrete
  // GPUs max(1 GiB, 6 % of the VRAM) — the driver, fragmentation and other
  // applications (llama.cpp's own fitter uses 1 GiB).  It was 2 GiB while
  // the KV estimate was a heuristic; now that KV is computed per layer and
  // the totals measured above the real peak on every tested model
  // (docs/NOTES-VRAM-FITTING.md), the margin no longer hides estimation
  // errors.  The CUDA context is already excluded from memory_free.
  bool integrated = bottleneck->is_integrated;
  constexpr uint64_t kMiB = 1024ULL * 1024;
  uint64_t effective_margin =
      integrated ? 512 * kMiB
                 : std::max(1024 * kMiB, bottleneck->memory_total * 6 / 100);
  if (bottleneck->memory_free <= effective_margin) {
    params.n_gpu_layers = 0;
    LB_FIT_LOG(qWarning) << "[LocalBackend] lb_fit: GPU" << bottleneck->name
               << "has insufficient free memory, CPU only";
    return params;
  }
  uint64_t available = bottleneck->memory_free - effective_margin;
  LB_FIT_LOG(qDebug) << "[LocalBackend] lb_fit:"
                     << (integrated ? "integrated" : "discrete") << "GPU"
                     << bottleneck->name << "| free"
                     << (bottleneck->memory_free >> 20) << "MiB | margin"
                     << (effective_margin >> 20) << "MiB | available"
                     << (available >> 20) << "MiB";

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
    int iGPU_type_v =
        lb_v_type(iGPU_type_k, usesFlashAttn(probe.is_hybrid));
    const QString iGPU_kv_name = QString("K=%1 V=%2")
                                     .arg(lb_kv_name(iGPU_type_k))
                                     .arg(lb_kv_name(iGPU_type_v));
    params.cache_type_k = static_cast<ggml_type>(iGPU_type_k);
    params.cache_type_v = static_cast<ggml_type>(iGPU_type_v);

    // Persistent GPU estimate: model weights + KV cache (quantized)
    uint64_t kv_size = lb_estimate_kv_cache(
        probe.kv_layers, probe.n_swa, probe.n_layers, desiredCtx,
        params.n_ubatch, iGPU_type_k, iGPU_type_v);
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
    uint32_t fit_ctx = pad256(desiredCtx);
    uint32_t floor_ctx = std::max(minCtx, 4096U);

    while (fit_ctx > pad256(floor_ctx)) {
      uint64_t fit_kv = lb_estimate_kv_cache(probe.kv_layers, probe.n_swa,
                                             probe.n_layers, fit_ctx,
                                             params.n_ubatch, iGPU_type_k,
                                             iGPU_type_v);
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
      fit_ctx = pad256(fit_ctx - 1024U);
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
  //   1. n_ubatch / 2 down to 2048, or 512 without flash attention (smaller
  //      compute buffer, slower prompt; without FA the KQ matrix shrinks
  //      with n_ubatch, so this is by far the cheapest reduction)
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
  uint32_t n_ctx = pad256(desiredCtx);
  const uint32_t ctx_floor = pad256(std::min(minCtx, n_ctx));
  uint32_t n_ubatch = desiredUbatch;
  int type_kv = GGML_TYPE_F16; // K type; V follows unless flash attn is off
  bool mmproj_on_gpu = probe.has_mmproj;
  // Without flash attention (hybrid models unless kHybridFlashAttn) the V
  // cache stays F16 and the KQ matrix is counted below.
  const bool flash_attn = usesFlashAttn(probe.is_hybrid);
  const uint32_t ubatch_floor_1 = flash_attn ? 2048U : 512U;

  for (int attempts = 0; attempts < 500; ++attempts) {
    const int n_gpu_layers = total_layers - offloaded;
    const int type_v = lb_v_type(type_kv, flash_attn);
    // GPU-side demand only: layers on the CPU keep their weights and KV in
    // system RAM (offload_kqv), so they cost nothing here.
    const uint64_t kv_size =
        lb_estimate_kv_cache(probe.kv_layers, probe.n_swa, n_gpu_layers,
                             n_ctx, n_ubatch, type_kv, type_v);
    const uint64_t gpu_weight = static_cast<uint64_t>(n_gpu_layers) * per_layer;
    const uint64_t mmproj_mem = mmproj_on_gpu ? probe.mmproj_size : 0;
    // CUDA graph reserve allocates per-layer activations.  Calibrated: 8433
    // MiB measured for 48 layers, 8192 ubatch, 3840 embd → ~6 bytes/element.
    // Bumped to 8 for headroom: sliding-window models (gemma4) allocate
    // extra SWA buffers.
    const uint64_t compute_base = static_cast<uint64_t>(n_ubatch) *
                                  std::max(probe.n_embd, 1) *
                                  static_cast<uint64_t>(n_gpu_layers) * 8;
    // Without flash attention, llama.cpp materializes one layer's attention
    // scores: F32, n_ctx × n_ubatch × n_head.  It grows with the context and
    // dominates at long prompts — measured on Qwen3.5-9B (hybrid, 16 heads),
    // n_ctx 43264, n_ubatch 2048: ~5360 MiB vs 5408 MiB from this formula.
    const uint64_t kq_buf =
        flash_attn || n_gpu_layers == 0
            ? 0
            : static_cast<uint64_t>(n_ctx) * n_ubatch *
                  static_cast<uint64_t>(std::max(probe.n_head, 1)) * 4;
    const uint64_t compute_buf = compute_base + kq_buf;
    const uint64_t gpu_total =
        (gpu_weight + mmproj_mem + kv_size + compute_buf) * 105 / 100;

    if (gpu_total <= available) {
      // llama.cpp counts the output layer in n_gpu_layers: the repeating
      // layers on the GPU start at n_layer + 1 - n_gpu_layers.  Passing the
      // bare layer count left layer 0 (weights + KV) on the CPU.  +1 puts
      // exactly the last n_gpu_layers layers, as estimated, plus the output
      // layer on the GPU.
      params.n_gpu_layers = n_gpu_layers > 0 ? n_gpu_layers + 1 : 0;
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
      // llama.cpp puts the last n_gpu_layers on the GPU device and the rest
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
               << "MiB | compute=" << (compute_buf >> 20) << "MiB (KQ "
               << (kq_buf >> 20) << ")";
      return params;
    }

    // ── OOM: apply the first reduction still possible ─────────────
    if (n_ubatch > ubatch_floor_1) { // 1
      const uint32_t old = n_ubatch;
      n_ubatch = std::max(n_ubatch / 2, ubatch_floor_1);
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
      n_ctx = std::max(pad256(n_ctx - n_ctx / 10), ctx_floor);
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

} // namespace VramFit
