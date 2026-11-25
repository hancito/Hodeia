#include "chatbackend.h"

#include "agentbackend.h"
#include "anthropicapi.h"
#include "configmanager.h"
#include "localbackend.h"
#include "remotebackend.h"

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <utility>

/* ── Active backend ─────────────────────────────────────────────── */

ActiveBackend resolveActiveBackend(ConfigManager *config) {
  ActiveBackend info;
  info.modelName = config->activeModel();
  info.config = config->backendConfig(config->activeBackend());
  if (config->activeBackend() == "Llama.cpp")
    info.kind = ActiveBackend::Kind::Local;
  else if (info.config.value("api").toString() == QLatin1String("anthropic"))
    info.kind = ActiveBackend::Kind::Anthropic;
  else if (info.config.value("api").toString() == QLatin1String("agent"))
    info.kind = ActiveBackend::Kind::Agent;
  else
    info.kind = ActiveBackend::Kind::OpenAi;

  if (info.kind == ActiveBackend::Kind::Local) {
    QString folderPath = config->summarizersPath() + "/" + info.modelName;
    QStringList ggufs =
        QDir(folderPath)
            .entryList(QStringList{"*.gguf"}, QDir::Files | QDir::NoSymLinks);
    for (const QString &f : ggufs) {
      if (!f.contains("mmproj", Qt::CaseInsensitive)) {
        info.modelPath = folderPath + "/" + f;
        break;
      }
    }
  }
  return info;
}

const char *backendKindName(ActiveBackend::Kind kind) {
  switch (kind) {
  case ActiveBackend::Kind::Local:
    return "local";
  case ActiveBackend::Kind::OpenAi:
    return "openai";
  case ActiveBackend::Kind::Anthropic:
    return "anthropic";
  case ActiveBackend::Kind::Agent:
    return "agent";
  }
  return "?";
}

ContextBudget estimateContextBudget(const ActiveBackend &active,
                                    int promptTokens,
                                    uint32_t responseHeadroom) {
  ContextBudget budget;
  if (active.kind != ActiveBackend::Kind::Local) {
    budget.remote = true;
    budget.autoTokens = ContextBudget::RemoteAutoTokens;
    budget.maxTokens = ContextBudget::RemoteMaxTokens;
    return budget;
  }
  const LocalBackend::ContextEstimate est = LocalBackend::estimateContext(
      active.modelPath, promptTokens, responseHeadroom);
  if (!est.valid) {
    budget.autoTokens = ContextBudget::LocalFallbackTokens;
    budget.maxTokens = ContextBudget::RemoteMaxTokens;
    return budget;
  }
  budget.autoTokens = est.autoTokens;
  budget.maxTokens = est.maxTokens;
  budget.truncated = est.truncated;
  switch (est.placement) {
  case LocalBackend::Placement::Gpu:
    budget.placement = ContextBudget::Placement::Gpu;
    break;
  case LocalBackend::Placement::Split:
    budget.placement = ContextBudget::Placement::Split;
    break;
  case LocalBackend::Placement::Cpu:
    budget.placement = ContextBudget::Placement::Cpu;
    break;
  }
  return budget;
}

namespace {

/* ── Local (llama.cpp) ──────────────────────────────────────────── */

class LocalChatBackend final : public ChatBackend {
public:
  explicit LocalChatBackend(QString modelPath)
      : m_modelPath(std::move(modelPath)) {}

  BackendCaps caps() const override { return {.usesLocalGpu = true}; }

  std::expected<QString, GenerationError>
  generate(const PromptPayload &payload, const GenerationOptions &options,
           bool *cancelFlag) override {
    if (m_modelPath.isEmpty())
      return std::unexpected(
          GenerationError{GenerationError::Kind::NoModelFile, {}});

    // The backend counts the exact prompt tokens during its no-alloc probe
    // and sizes the KV cache as prompt_tokens + headroom.
    qDebug() << "[ChatBackend] Loading local model:" << m_modelPath;
    QElapsedTimer loadTimer;
    loadTimer.start();
    auto loaded =
        m_backend.loadModel(m_modelPath, payload, options.responseHeadroom);
    if (!loaded) {
      qWarning() << "[ChatBackend] Failed to load model:" << loaded.error();
      return std::unexpected(
          GenerationError{GenerationError::Kind::LoadFailed, loaded.error()});
    }
    qDebug() << "[ChatBackend] Model loaded in" << loadTimer.elapsed() << "ms";

    QElapsedTimer genTimer;
    genTimer.start();
    // The cancel flag lets a queued stop() interrupt a long generation at the
    // next token boundary (see InferenceEngine::generate).
    QString result = m_backend.summarize(payload, cancelFlag);
    qDebug() << "[ChatBackend] Generation completed in" << genTimer.elapsed()
             << "ms (" << result.length() << "chars)"
             << (cancelFlag && *cancelFlag ? "— CANCELLED" : "");
    return result;
  }

  void release() override {
    m_backend.unloadModel();
    qDebug() << "[ChatBackend] Chat model unloaded";
  }

private:
  QString m_modelPath;
  LocalBackend m_backend;
};

/* ── Remote (OpenAI-compatible endpoint) ────────────────────────── */

class RemoteChatBackend final : public ChatBackend {
public:
  RemoteChatBackend(const QString &modelName, const QVariantMap &config) {
    m_backend.setApiUrl(config.value("url").toString());
    m_backend.setApiKey(config.value("api_key").toString());
    m_backend.setModelName(modelName);
  }

  BackendCaps caps() const override { return {}; }

  std::expected<QString, GenerationError>
  generate(const PromptPayload &payload, const GenerationOptions &options,
           bool * /*cancelFlag: cancel() aborts the request instead*/) override {
    m_backend.setForceJson(options.forceJson);

    // Images travel with their page.
    bool hasImages = false;
    for (const auto &pb : payload.pageBlocks)
      if (!pb.visualContext.data.empty())
        hasImages = true;

    qDebug() << "[ChatBackend] Remote request — URL:" << m_backend.apiUrl()
             << "| Model:" << m_backend.modelName()
             << "| images:" << (hasImages ? "yes" : "no");
    QElapsedTimer genTimer;
    genTimer.start();

    // Synchronous wait for the asynchronous request.
    QString result, error;
    bool timedOut = false;
    QEventLoop loop;
    QObject::connect(
        &m_backend, &RemoteBackend::finished, &loop,
        [&result, &error, &loop](const QString &text, const QString &err) {
          result = text;
          error = err;
          loop.quit();
        },
        Qt::SingleShotConnection);

    // Watchdog: a hung endpoint would otherwise block the worker thread
    // forever.
    QTimer timeout;
    if (options.timeoutMs > 0) {
      timeout.setSingleShot(true);
      QObject::connect(&timeout, &QTimer::timeout, &loop,
                       [&loop, &timedOut]() {
                         timedOut = true;
                         if (loop.isRunning())
                           loop.quit();
                       });
      timeout.start(options.timeoutMs);
    }

    m_backend.generateInterleaved(payload.systemPrompt, payload, hasImages);

    loop.exec(QEventLoop::ExcludeUserInputEvents);
    timeout.stop();

    qDebug() << "[ChatBackend] Remote generation completed in"
             << genTimer.elapsed() << "ms (" << result.length() << "chars)"
             << (timedOut ? "— TIMED OUT" : "");

    if (timedOut) {
      // The reply never finished: cancel it so it does not fire later into a
      // dead loop.
      m_backend.cancel();
      qWarning() << "[ChatBackend] Remote request timed out after"
                 << options.timeoutMs / 1000 << "s";
      return std::unexpected(GenerationError{GenerationError::Kind::Timeout, {}});
    }
    if (!error.isEmpty()) {
      qWarning() << "[ChatBackend] Remote request failed:" << error;
      return std::unexpected(
          GenerationError{GenerationError::Kind::RequestFailed, error});
    }
    return result;
  }

  void cancel() override { m_backend.cancel(); }

private:
  RemoteBackend m_backend;
};

/* ── Anthropic Messages API ─────────────────────────────────────── */

class AnthropicChatBackend final : public ChatBackend {
public:
  AnthropicChatBackend(const QString &modelName, const QVariantMap &config)
      : m_modelName(modelName), m_url(config.value("url").toString()),
        m_apiKey(config.value("api_key").toString()) {}

  BackendCaps caps() const override { return {}; }

  std::expected<QString, GenerationError>
  generate(const PromptPayload &payload, const GenerationOptions &options,
           bool * /*cancelFlag: cancel() aborts the request instead*/) override {
    // options.forceJson: the Messages API has no plain "JSON mode"; the
    // summary prompt already asks for JSON only, and SummaryFormat extracts
    // the object from the text.
    QNetworkRequest request(
        AnthropicApi::endpointUrl(m_url, QStringLiteral("messages")));
    AnthropicApi::applyHeaders(request, m_apiKey, m_modelName);
    const QByteArray body =
        QJsonDocument(AnthropicApi::buildBody(payload, m_modelName))
            .toJson(QJsonDocument::Compact);
    qDebug() << "[ChatBackend] Anthropic request —" << request.url().toString()
             << "| Model:" << m_modelName << "|" << body.size() / 1024
             << "KiB";

    QElapsedTimer genTimer;
    genTimer.start();
    QEventLoop loop;
    bool timedOut = false;
    QByteArray answer;
    int status = 0;
    QString networkError;

    m_reply = m_manager.post(request, body);
    QObject::connect(m_reply, &QNetworkReply::finished, &loop,
                     [&, reply = m_reply]() {
                       if (m_reply == reply)
                         m_reply = nullptr;
                       status = reply->attribute(
                                         QNetworkRequest::HttpStatusCodeAttribute)
                                    .toInt();
                       answer = reply->readAll();
                       if (reply->error() != QNetworkReply::NoError)
                         networkError = reply->errorString();
                       reply->deleteLater();
                       loop.quit();
                     });

    QTimer timeout;
    if (options.timeoutMs > 0) {
      timeout.setSingleShot(true);
      QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        cancel();
      });
      timeout.start(options.timeoutMs);
    }
    // finished may already have fired (immediate failure): the loop would
    // then wait forever.
    if (m_reply)
      loop.exec(QEventLoop::ExcludeUserInputEvents);
    timeout.stop();

    qDebug() << "[ChatBackend] Anthropic response in" << genTimer.elapsed()
             << "ms | HTTP" << status << "|" << answer.size() << "bytes"
             << (timedOut ? "— TIMED OUT" : "");

    if (timedOut)
      return std::unexpected(GenerationError{GenerationError::Kind::Timeout, {}});
    // The API answers errors with a JSON body: prefer its message over the
    // generic network error.
    auto parsed = AnthropicApi::parseResponse(answer, status);
    if (parsed)
      return *parsed;
    const QString detail = !answer.isEmpty() || networkError.isEmpty()
                               ? parsed.error()
                               : networkError;
    qWarning() << "[ChatBackend] Anthropic request failed:" << detail;
    return std::unexpected(
        GenerationError{GenerationError::Kind::RequestFailed, detail});
  }

  void cancel() override {
    // abort() emits finished synchronously (the handler clears m_reply and
    // ends the wait loop): take the pointer first.
    if (QNetworkReply *reply = std::exchange(m_reply, nullptr))
      reply->abort();
  }

private:
  QString m_modelName;
  QString m_url;
  QString m_apiKey;
  QNetworkAccessManager m_manager;
  QNetworkReply *m_reply = nullptr;
};

} // namespace

std::unique_ptr<ChatBackend> makeChatBackend(const ActiveBackend &active) {
  switch (active.kind) {
  case ActiveBackend::Kind::Local:
    return std::make_unique<LocalChatBackend>(active.modelPath);
  case ActiveBackend::Kind::Anthropic:
    return std::make_unique<AnthropicChatBackend>(active.modelName,
                                                  active.config);
  case ActiveBackend::Kind::Agent:
    return AgentBackend::make(active.modelName, active.config);
  case ActiveBackend::Kind::OpenAi:
    break;
  }
  return std::make_unique<RemoteChatBackend>(active.modelName, active.config);
}
