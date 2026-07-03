#include "headers/llama_chat_backend.h"

#include "headers/llama_inference_engine.h"

#include "llama.h"
#include "mtmd.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <array>
#include <vector>

/// Static — llama_backend init/once.
static bool s_llamaInitialized = false;

LlamaChatBackend::LlamaChatBackend(QObject *parent) : QObject(parent) {
  if (!s_llamaInitialized) {
    ggml_backend_load_all();
    llama_backend_init();
    s_llamaInitialized = true;
  }
}

LlamaChatBackend::~LlamaChatBackend() {
  unloadModel();
  if (s_llamaInitialized) {
    llama_backend_free();
    s_llamaInitialized = false;
  }
}

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

  // ── Load model ──
  llama_model_params modelParams = llama_model_default_params();
  modelParams.n_gpu_layers = 99;

  m_llamaModel =
      llama_model_load_from_file(path.toUtf8().constData(), modelParams);
  if (!m_llamaModel) {
    qCritical() << "[LlamaChatBackend] Failed to load model:" << path;
    emit statusUpdated("Failed to load model");
    return false;
  }

  bool useGpu = (modelParams.n_gpu_layers > 0);
  qDebug() << "[LlamaChatBackend] GPU layers:" << modelParams.n_gpu_layers;

  // ── Create context ──
  llama_context_params ctxParams = llama_context_default_params();
  ctxParams.n_ctx = MaxPromptTokens + MaxGenTokens;
  ctxParams.n_batch = MaxPromptTokens + MaxGenTokens;
  ctxParams.n_ubatch = 2048;
  ctxParams.n_threads = QThread::idealThreadCount();
  ctxParams.n_threads_batch = QThread::idealThreadCount();

  m_llamaCtx = llama_init_from_model(m_llamaModel, ctxParams);
  if (!m_llamaCtx) {
    qCritical() << "[LlamaChatBackend] Failed to create context";
    llama_model_free(m_llamaModel);
    m_llamaModel = nullptr;
    return false;
  }

  m_modelPath = path;

  // ── Load mmproj (multimodal projector) ──
  m_mmprojPath = resolveMmprojPath(path);
  if (!m_mmprojPath.isEmpty()) {
    emit statusUpdated("Loading multimodal projector: " + m_mmprojPath);

    auto mtmdParams = mtmd_context_params_default();
    mtmdParams.use_gpu = useGpu;
    mtmdParams.n_threads = QThread::idealThreadCount();
    mtmdParams.warmup = false;

    m_mtmdCtx = mtmd_init_from_file(m_mmprojPath.toUtf8().constData(),
                                    m_llamaModel, mtmdParams);
    if (m_mtmdCtx) {
      m_hasVision = mtmd_support_vision(m_mtmdCtx);
      qDebug() << "[LlamaChatBackend] Vision support:" << m_hasVision;
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

  int32_t ctxSize = llama_n_ctx(m_llamaCtx);
  int32_t maxPromptToks = std::max(1, ctxSize - MaxGenTokens);
  int maxChars = maxPromptToks * 3;

  QString promptText = buildFormattedPrompt(payload, maxChars);

  if (m_hasVision && m_mtmdCtx && !payload.visualContext.empty()) {
    return runMultimodalInference(promptText, payload.visualContext);
  }
  return runTextInference(promptText);
}

QString LlamaChatBackend::generate(const QString &systemPrompt,
                                   const QString &userPrompt) {
  PromptPayload p;
  p.systemPrompt = systemPrompt;
  p.userMessage = userPrompt;
  p.textContext = userPrompt;
  return summarize(p);
}

// ── Text-only inference ────────────────────────────────────────────

QString LlamaChatBackend::runTextInference(const QString &formattedText) {
  InferenceParams params;
  params.max_tokens = MaxGenTokens;
  params.temperature = Temperature;
  params.top_k = TopK;
  params.top_p = TopP;

  LlamaInferenceEngine engine(m_llamaCtx, m_llamaModel, params);
  QString result = engine.inferText(formattedText);

  if (result.isEmpty()) {
    qWarning() << "[LlamaChatBackend] Text inference returned empty output";
  }
  return result;
}

// ── Multimodal inference ──────────────────────────────────────────

QString
LlamaChatBackend::runMultimodalInference(const QString &formattedText,
                                         const std::vector<PageImage> &images) {
  if (!m_mtmdCtx || !m_hasVision) {
    qWarning() << "[LlamaChatBackend] Multimodal inference requested but "
                  "vision not available";
    return QString();
  }

  // PageImage and RawImage share the same layout (width, height, data).
  // The engine handles mtmd_bitmap creation internally.
  std::vector<RawImage> rawImages;
  rawImages.reserve(images.size());
  for (const auto &img : images) {
    rawImages.push_back({img.width, img.height, img.data});
  }

  qDebug() << "[LlamaChatBackend] Multimodal inference with" << rawImages.size()
           << "images";

  InferenceParams params;
  params.max_tokens = MaxGenTokens;
  params.temperature = Temperature;
  params.top_k = TopK;
  params.top_p = TopP;

  LlamaInferenceEngine engine(m_llamaCtx, m_llamaModel, params);
  QString result = engine.inferMultimodal(formattedText, rawImages, m_mtmdCtx);

  if (result.isEmpty()) {
    qWarning()
        << "[LlamaChatBackend] Multimodal inference returned empty output";
  }
  return result;
}

// ── Prompt building ────────────────────────────────────────────────

QString LlamaChatBackend::buildFormattedPrompt(const PromptPayload &payload,
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

QString LlamaChatBackend::applyChatTemplate(const QString &systemPrompt,
                                            const QString &userMessage) {
  struct llama_chat_message chat[2];
  int nMsg = 0;

  QByteArray sysBytes = systemPrompt.toUtf8();
  QByteArray userBytes = userMessage.toUtf8();

  if (!systemPrompt.isEmpty()) {
    chat[nMsg].role = "system";
    chat[nMsg].content = sysBytes.constData();
    nMsg++;
  }
  chat[nMsg].role = "user";
  chat[nMsg].content = userBytes.constData();
  nMsg++;

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

// ── Path resolution ────────────────────────────────────────────────

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
      qDebug() << "[LlamaChatBackend] Found mmproj:" << c;
      return c;
    }
  }

  qDebug() << "[LlamaChatBackend] No mmproj found for model:" << modelPath;
  return QString();
}
