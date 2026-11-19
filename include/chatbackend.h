#pragma once

#include "localprompt.h"

#include <QString>
#include <QVariantMap>
#include <cstdint>
#include <expected>
#include <memory>

class ConfigManager;

/// The backend and model a generation would use right now, from the user's
/// settings (Backends window).
struct ActiveBackend {
  enum class Kind {
    Local,     ///< llama.cpp on this machine
    OpenAi,    ///< remote OpenAI-compatible endpoint (/v1/chat/completions)
    Anthropic, ///< remote Anthropic Messages API (/v1/messages)
    Agent,     ///< coding agent (Claude Code, Qwen Code, Pi...), see AgentBackend
  };
  /// Local by default: nothing leaves the machine unless the settings say so.
  Kind kind = Kind::Local;
  QString modelName;  ///< model folder (local) or model id (remote)
  QString modelPath;  ///< local only: the .gguf to load (empty if none)
  QVariantMap config; ///< remote only: url, api_key, api ("openai",
                      ///< "anthropic" or "agent": see AgentBackend)
};

/// Resolve the active backend and, for a local one, its .gguf file.
ActiveBackend resolveActiveBackend(ConfigManager *config);

/// Short name of a backend kind, for the logs.
const char *backendKindName(ActiveBackend::Kind kind);

/// Prompt budget of a backend, before any model is loaded.
struct ContextBudget {
  /// Prompt budget used for remote backends in "auto" mode (their context
  /// size is unknown to the app).
  static constexpr int RemoteAutoTokens = 16384;
  /// Upper bound offered for remote backends.
  static constexpr int RemoteMaxTokens = 262144;
  /// Local fallback when the model cannot be read.
  static constexpr int LocalFallbackTokens = 8192;

  enum class Placement { Unknown, Gpu, Split, Cpu };

  bool remote = false;
  int autoTokens = 0; ///< default budget (local: largest fully on the GPU)
  int maxTokens = 0;  ///< largest budget offered
  Placement placement = Placement::Unknown; ///< local, for `promptTokens`
  bool truncated = false; ///< local: `promptTokens` won't fit, prompt cut
};

/// Budget of `active` for a prompt of `promptTokens` (0 = the auto budget)
/// plus `responseHeadroom`.  Local: same memory fitting as the model load.
ContextBudget estimateContextBudget(const ActiveBackend &active,
                                    int promptTokens,
                                    uint32_t responseHeadroom);

/// Why a generation failed.  The callers word the user-facing message, each
/// in its own translation context.
struct GenerationError {
  enum class Kind {
    NoModelFile,   ///< local: no .gguf in the model folder
    LoadFailed,    ///< local: the model could not be loaded (detail = cause)
    Timeout,       ///< remote: no answer within GenerationOptions::timeoutMs
    RequestFailed, ///< remote: the request failed (detail = cause)
  };
  Kind kind;
  QString detail;
};

struct GenerationOptions {
  /// Local: tokens kept free in the KV cache for the response.
  uint32_t responseHeadroom = 2048;
  /// Remote: ask for a JSON response (structured outputs, e.g. summaries).
  bool forceJson = false;
  /// Remote: abort after this many milliseconds; 0 = wait indefinitely.
  int timeoutMs = 0;
};

/// What a backend needs from, or offers to, its callers.
struct BackendCaps {
  /// Runs on this machine's GPU: a summary batch unloads the embedding model
  /// to free VRAM, and release() frees the chat model afterwards.
  bool usesLocalGpu = false;
};

/// A chat-completion backend: the same PromptPayload goes to a local
/// llama.cpp model or to a remote endpoint (OpenAI-compatible, Anthropic).
///
/// Used synchronously from a worker thread: generate() returns when the text
/// is complete.  cancel() may be called meanwhile (from a slot run by the
/// event loop generate() pumps) to abort a remote request; a local
/// generation is stopped through `cancelFlag`.
class ChatBackend {
public:
  virtual ~ChatBackend() = default;

  virtual BackendCaps caps() const = 0;

  /// Generate a completion.  On cancellation the partial (possibly empty)
  /// text is returned: callers check their own cancel flag first.
  virtual std::expected<QString, GenerationError>
  generate(const PromptPayload &payload, const GenerationOptions &options,
           bool *cancelFlag) = 0;

  /// Abort an in-flight remote request.  No-op for a local backend.
  virtual void cancel() {}

  /// Free the model memory (local) once the run is over.  No-op remotely.
  virtual void release() {}
};

/// The backend matching `active`.
std::unique_ptr<ChatBackend> makeChatBackend(const ActiveBackend &active);
