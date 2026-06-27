#include "headers/localbackend.h"

#include "llama.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QThread>
#include <vector>

/// Static — llama_backend_init is called once on first construction.
static bool s_llamaInitialized = false;

LocalBackend::LocalBackend(QObject *parent) : QObject(parent) {
  if (!s_llamaInitialized) {
    llama_backend_init();
    s_llamaInitialized = true;
    qDebug() << "[LocalBackend] llama backend initialized"
             << llama_print_system_info();
  }
}

LocalBackend::~LocalBackend() { unloadModel(); }

bool LocalBackend::loadModel(const QString &modelPath) {
  unloadModel();

  QString path = resolveModelPath(modelPath);
  if (path.isEmpty() || !QFile::exists(path)) {
    qCritical() << "[LocalBackend] Model file not found:" << path;
    emit statusUpdated("Model file not found: " + path);
    return false;
  }

  emit statusUpdated("Loading model: " + path);

  llama_model_params modelParams = llama_model_default_params();
  modelParams.n_gpu_layers = 99; // offload as many layers as possible
  modelParams.progress_callback = nullptr;

  m_llamaModel =
      llama_model_load_from_file(path.toUtf8().constData(), modelParams);
  if (!m_llamaModel) {
    qCritical() << "[LocalBackend] Failed to load model:" << path;
    emit statusUpdated("Failed to load model");
    return false;
  }

  llama_context_params ctxParams = llama_context_default_params();
  ctxParams.n_ctx = MaxPromptTokens + MaxGenTokens;
  ctxParams.n_batch = MaxPromptTokens;
  ctxParams.n_ubatch = MaxPromptTokens;
  ctxParams.n_threads = QThread::idealThreadCount();
  ctxParams.n_threads_batch = -1;
  ctxParams.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_ENABLED;

  m_llamaCtx = llama_init_from_model(m_llamaModel, ctxParams);
  if (!m_llamaCtx) {
    qCritical() << "[LocalBackend] Failed to create context";
    emit statusUpdated("Failed to create context");
    llama_model_free(m_llamaModel);
    m_llamaModel = nullptr;
    return false;
  }

  m_modelPath = path;
  m_ready = true;

  uint32_t ctxSize = llama_n_ctx(m_llamaCtx);
  qDebug() << "[LocalBackend] Model loaded. ctx=" << ctxSize;
  emit statusUpdated("Model loaded: " + path);
  return true;
}

bool LocalBackend::isReady() const { return m_ready; }

void LocalBackend::unloadModel() {
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
}

QString LocalBackend::generate(const QString &systemPrompt,
                               const QString &userPrompt) {
  if (!m_ready) {
    qWarning() << "[LocalBackend] Model not loaded";
    return QString();
  }

  const auto *vocab = llama_model_get_vocab(m_llamaModel);

  // Build chat messages for template application
  struct llama_chat_message chat[2];
  int nMsg = 0;

  if (!systemPrompt.isEmpty()) {
    chat[nMsg].role = "system";
    chat[nMsg].content = systemPrompt.toUtf8().constData();
    nMsg++;
  }
  chat[nMsg].role = "user";
  chat[nMsg].content = userPrompt.toUtf8().constData();
  nMsg++;

  // Apply chat template
  const char *tmpl = llama_model_chat_template(m_llamaModel, nullptr);
  if (!tmpl) {
    qWarning() << "[LocalBackend] No chat template found in model";
    return QString();
  }

  char promptBuf[16384];
  int32_t res = llama_chat_apply_template(
      tmpl, chat, static_cast<size_t>(nMsg), true, promptBuf,
      static_cast<int32_t>(sizeof(promptBuf)));
  if (res < 0) {
    qWarning() << "[LocalBackend] Chat template failed";
    return QString();
  }

  QString promptStr = QString::fromUtf8(promptBuf, res);
  qDebug() << "[LocalBackend] Prompt length:" << promptStr.length();

  // Tokenize
  std::vector<llama_token> tokens;
  tokens.reserve(MaxPromptTokens);

  QByteArray promptBytes = promptStr.toUtf8();
  int nTok = llama_tokenize(vocab, promptBytes.constData(), promptBytes.size(),
                            tokens.data(), static_cast<int>(tokens.capacity()),
                            true, true);
  if (nTok < 0) {
    nTok = -nTok; // needs more space, re-allocate
    tokens.resize(nTok);
    nTok = llama_tokenize(vocab, promptBytes.constData(), promptBytes.size(),
                          tokens.data(), static_cast<int>(tokens.size()), true,
                          true);
  }
  tokens.resize(nTok);

  if (static_cast<int>(tokens.size()) > MaxPromptTokens) {
    tokens.resize(MaxPromptTokens);
  }

  // Build sampler chain
  auto *smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
  llama_sampler_chain_add(smpl, llama_sampler_init_top_k(TopK));
  llama_sampler_chain_add(smpl, llama_sampler_init_top_p(TopP, 1));
  if (Temperature > 0.0f) {
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(Temperature));
  } else {
    llama_sampler_chain_add(smpl, llama_sampler_init_greedy());
  }
  llama_sampler_chain_add(smpl, llama_sampler_init_dist(1234));

  // Clear KV cache
  llama_memory_clear(llama_get_memory(m_llamaCtx), true);

  // Decode prompt — build batch manually (llama_batch_add removed in new API)
  int32_t batchSize = static_cast<int32_t>(tokens.size()) + MaxGenTokens;
  llama_batch batch = llama_batch_init(batchSize, 0, 1);

  batch.n_tokens = static_cast<int32_t>(tokens.size());
  for (int32_t i = 0; i < batch.n_tokens; i++) {
    batch.token[i] = tokens[i];
    batch.pos[i] = i;
    batch.seq_id[i][0] = 0;
    batch.n_seq_id[i] = 1;
    batch.logits[i] = 0;
  }

  if (llama_decode(m_llamaCtx, batch) != 0) {
    qWarning() << "[LocalBackend] Prompt decode failed";
    llama_sampler_free(smpl);
    llama_batch_free(batch);
    return QString();
  }

  // Generation loop
  std::vector<llama_token> generated;
  generated.reserve(MaxGenTokens);
  llama_token eosTok = llama_vocab_eos(vocab);
  bool hitEos = false;

  for (int i = 0; i < MaxGenTokens; i++) {
    // Sample next token from the last position
    llama_token nextTok = llama_sampler_sample(
        smpl, m_llamaCtx,
        static_cast<int32_t>(tokens.size() - 1 + generated.size()));
    generated.push_back(nextTok);

    if (nextTok == eosTok || i == MaxGenTokens - 1) {
      hitEos = true;
      break;
    }

    // Decode the single new token — manual batch construction
    int32_t pos = static_cast<int32_t>(tokens.size() + generated.size() - 1);
    llama_batch single = llama_batch_init(1, 0, 1);
    single.n_tokens = 1;
    single.token[0] = nextTok;
    single.pos[0] = pos;
    single.seq_id[0][0] = 0;
    single.n_seq_id[0] = 1;
    single.logits[0] = 1;

    if (llama_decode(m_llamaCtx, single) != 0) {
      qWarning() << "[LocalBackend] Generation decode failed at step" << i;
      llama_batch_free(single);
      break;
    }
    llama_batch_free(single);
  }

  // Detokenize
  QString result;
  for (llama_token t : generated) {
    char buf[32];
    int len = llama_token_to_piece(vocab, t, buf,
                                   static_cast<int32_t>(sizeof(buf)), 0, true);
    if (len > 0) {
      result.append(QString::fromUtf8(buf, len));
    }
  }

  llama_sampler_free(smpl);
  llama_batch_free(batch);

  qDebug() << "[LocalBackend] Generated" << generated.size() << "tokens"
           << (hitEos ? "(EOS)" : "(max_tokens)");
  emit statusUpdated(QString("Generated %1 tokens").arg(generated.size()));

  return result;
}

QString LocalBackend::resolveModelPath(const QString &modelPath) const {
  if (modelPath.isEmpty()) {
    // Default: Models/Summarizers/default.gguf
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
