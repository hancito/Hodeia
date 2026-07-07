#include "headers/localbackend.h"

#include "headers/inferenceengine.h"

#include "common/chat.h"
#include "common/common.h"
#include "llama.h"
#include "mtmd.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
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
bool LocalBackend::loadModel(const QString &modelPath) {
  unloadModel();

  QString path = resolveModelPath(modelPath);
  if (path.isEmpty() || !QFile::exists(path)) {
    qCritical() << "[LocalBackend] Model file not found:" << path;
    emit statusUpdated("Model file not found: " + path);
    return false;
  }

  emit statusUpdated("Loading model: " + path);

  // ── Detect mmproj (lazy-loaded on first multimodal use) ──────────
  m_mmprojPath = resolveMmprojPath(path);
  m_hasVision = !m_mmprojPath.isEmpty();

  // ── Detect target backend device ─────────────────────────────────
  ggml_backend_dev_t dev = detectBestDevice();
  struct ggml_backend_dev_props props;
  ggml_backend_dev_get_props(dev, &props);

  // Disambiguate CUDA vs Vulkan: both may report DEVICE_TYPE_GPU on NVIDIA.
  // Check the device name string.
  const char *devName = props.name ? props.name : "";
  bool useCuda = strstr(devName, "CUDA") || strstr(devName, "cuda");
  bool useVulkan = strstr(devName, "Vulkan") || strstr(devName, "vulkan");
  bool useCpu = !useCuda && !useVulkan;

  common_init_result_ptr result;

  if (useCuda) {
    result = loadModelCuda(path, m_mmprojPath);
  } else if (useVulkan) {
    result = loadModelVulkan(path, m_mmprojPath);
  } else {
    result = loadModelCpu(path, m_mmprojPath);
  }

  if (!result)
    return false;

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

  qDebug() << "[LocalBackend] Model loaded | ctx:"
           << llama_n_ctx(m_llamaCtx) << "| batch:" << llama_n_batch(m_llamaCtx)
           << "| ubatch:" << llama_n_ubatch(m_llamaCtx)
           << "| vision:" << m_hasVision;
  emit statusUpdated("Model loaded: " + path);
  return true;
}

// ── Backend-specific model loaders ─────────────────────────────────

/// Detect the best available GPU device (CUDA > Vulkan > CPU).
ggml_backend_dev_t LocalBackend::detectBestDevice() {
  // Iterate all devices and prefer CUDA > Vulkan > CPU.
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
    qDebug() << "[LocalBackend] Device" << i << "—" << name << "["
             << type_str << "]";

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

  if (cudaDev) {
    qDebug() << "[LocalBackend] Detected CUDA device";
    return cudaDev;
  }
  if (vulkanDev) {
    qDebug() << "[LocalBackend] Detected Vulkan device";
    return vulkanDev;
  }
  qDebug() << "[LocalBackend] No GPU detected, falling back to CPU";
  return cpuDev;
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
    params.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_DISABLED;
    qDebug() << "[LocalBackend] Flash attention disabled (hybrid model)";
    result = common_init_from_params(params);
    if (!result || !result->model() || !result->context())
      return nullptr;
  }
  return result;
}

/// ── CUDA-optimized loader ────────────────────────────────────────
common_init_result_ptr
LocalBackend::loadModelCuda(const QString &path,
                                const QString &mmprojPath) {
  qDebug() << "[LocalBackend] Loading model on CUDA";

  common_params params;
  params.model.path = path.toStdString();
  params.n_gpu_layers = -1;
  params.n_ctx = MaxPromptTokens + MaxGenTokens;
  params.n_batch = 8192;
  params.n_ubatch = 8192;
  params.cpuparams.n_threads = QThread::idealThreadCount();
  params.cpuparams_batch.n_threads = QThread::idealThreadCount();

  params.fit_params = true;
  params.fit_params_print = true;
  params.fit_params_min_ctx = 4096;
  params.fit_params_target.assign(llama_max_devices(), 1024ULL * 1024 * 1024);

  params.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_AUTO;

  if (!mmprojPath.isEmpty()) {
    params.mmproj.path = mmprojPath.toStdString();
    params.mmproj_use_gpu = true;
    qDebug() << "[LocalBackend] mmproj found, will lazy-load:"
             << mmprojPath;
  }

  auto result = createInitResult(params, true);
  if (!result) {
    qCritical() << "[LocalBackend] CUDA model load failed";
    emit statusUpdated("Failed to create context on CUDA");
    return nullptr;
  }

  qDebug() << "[LocalBackend] CUDA model loaded | ctx:"
           << llama_n_ctx(result->context())
           << "| batch:" << llama_n_batch(result->context())
           << "| ubatch:" << llama_n_ubatch(result->context());
  return result;
}

/// ── Vulkan-optimized loader ──────────────────────────────────────
common_init_result_ptr
LocalBackend::loadModelVulkan(const QString &path,
                                  const QString &mmprojPath) {
  qDebug() << "[LocalBackend] Loading model on Vulkan";

  common_params params;
  params.model.path = path.toStdString();
  params.n_gpu_layers = -1;
  params.n_ctx = MaxPromptTokens + MaxGenTokens;
  // Vulkan: smaller batches to avoid 4 GiB single allocations that fail.
  params.n_batch = 4096;
  params.n_ubatch = 2048;
  params.cpuparams.n_threads = QThread::idealThreadCount();
  params.cpuparams_batch.n_threads = QThread::idealThreadCount();

  // Vulkan needs a larger safety margin (pipeline state + descriptors).
  params.fit_params = true;
  params.fit_params_print = true;
  params.fit_params_min_ctx = 4096;
  params.fit_params_target.assign(llama_max_devices(), 2560ULL * 1024 * 1024);

  // Flash attention disabled — unstable on Vulkan with hybrid models
  // and offers no measurable benefit at our batch sizes.
  params.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_DISABLED;

  if (!mmprojPath.isEmpty()) {
    params.mmproj.path = mmprojPath.toStdString();
    params.mmproj_use_gpu = true;
    qDebug() << "[LocalBackend] mmproj found, will lazy-load:"
             << mmprojPath;
  }

  auto result = createInitResult(params, true);
  if (!result) {
    qCritical() << "[LocalBackend] Vulkan model load failed";
    emit statusUpdated("Failed to create context on Vulkan");
    return nullptr;
  }

  qDebug() << "[LocalBackend] Vulkan model loaded | ctx:"
           << llama_n_ctx(result->context())
           << "| batch:" << llama_n_batch(result->context())
           << "| ubatch:" << llama_n_ubatch(result->context());
  return result;
}

/// ── CPU-optimized loader ─────────────────────────────────────────
common_init_result_ptr
LocalBackend::loadModelCpu(const QString &path, const QString &mmprojPath) {
  qDebug() << "[LocalBackend] Loading model on CPU";

  common_params params;
  params.model.path = path.toStdString();
  params.n_gpu_layers = 0;
  params.n_ctx = MaxPromptTokens + MaxGenTokens;
  params.n_batch = 4096;
  params.n_ubatch = 2048;
  params.cpuparams.n_threads = QThread::idealThreadCount();
  params.cpuparams_batch.n_threads = QThread::idealThreadCount();

  params.fit_params = false;
  params.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_DISABLED;

  if (!mmprojPath.isEmpty()) {
    params.mmproj.path = mmprojPath.toStdString();
    params.mmproj_use_gpu = false;
    qDebug() << "[LocalBackend] mmproj found, will lazy-load on CPU:"
             << mmprojPath;
  }

  auto result = createInitResult(params, false);
  if (!result) {
    qCritical() << "[LocalBackend] CPU model load failed";
    emit statusUpdated("Failed to create context on CPU");
    return nullptr;
  }

  qDebug() << "[LocalBackend] CPU model loaded | ctx:"
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
  int32_t ctxSize = llama_n_ctx(m_llamaCtx);
  int32_t maxPromptToks = std::max(1, ctxSize - MaxGenTokens);
  int maxChars = maxPromptToks * 3;

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

QString LocalBackend::runTextInference(const QString &formattedText) {
  InferenceParams params;
  params.max_tokens = MaxGenTokens;
  params.temperature = Temperature;
  params.top_k = TopK;
  params.top_p = TopP;

  InferenceEngine engine(m_llamaCtx, m_llamaModel, params);
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
  if (!m_mtmdCtx && !loadMmproj()) {
    qWarning() << "[LocalBackend] Failed to lazy-load mmproj";
    return QString();
  }

  qDebug() << "[LocalBackend] Multimodal inference with" << images.size()
           << "images";

  InferenceParams params;
  params.max_tokens = MaxGenTokens;
  params.temperature = Temperature;
  params.top_k = TopK;
  params.top_p = TopP;

  QString result;
  {
    // Engine is scoped here so it's destroyed (and its sampler chain freed)
    // before we unload the mmproj — avoids use-after-free if the engine
    // holds references into the mtmd context.
    InferenceEngine engine(m_llamaCtx, m_llamaModel, params);
    result = engine.inferMultimodal(formattedText, images, m_mtmdCtx);
  }

  // Unload mmproj to free GPU VRAM after inference.
  // This is critical: the mmproj can take 100-300 MiB of VRAM.  Keeping it
  // around for the next text-only inference would be wasteful.
  unloadMmproj();

  if (result.isEmpty()) {
    qWarning()
        << "[LocalBackend] Multimodal inference returned empty output";
  }
  return result;
}

// ── Lazy mmproj load / unload ─────────────────────────────────────

bool LocalBackend::loadMmproj() {
  if (m_mtmdCtx)
    return true;
  if (m_mmprojPath.isEmpty())
    return false;

  emit statusUpdated("Loading multimodal projector: " + m_mmprojPath);

  auto mtmdParams = mtmd_context_params_default();
  mtmdParams.use_gpu = true;
  mtmdParams.n_threads = QThread::idealThreadCount();
  mtmdParams.warmup = false;

  m_mtmdCtx = mtmd_init_from_file(m_mmprojPath.toUtf8().constData(),
                                  m_llamaModel, mtmdParams);
  if (!m_mtmdCtx) {
    qWarning() << "[LocalBackend] Failed to load mmproj:" << m_mmprojPath;
    return false;
  }

  qDebug() << "[LocalBackend] mmproj loaded on GPU";
  return true;
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
  if (ctx.length() > maxChars) {
    ctx = ctx.left(maxChars) + "\n\n... [truncated]";
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
