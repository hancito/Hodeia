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
  bool isLocal = false;  ///< llama.cpp on this machine, else a remote endpoint
  QString modelName;     ///< model folder (local) or model id (remote)
  QString modelPath;     ///< local only: the .gguf to load (empty if none)
  QVariantMap config;    ///< remote only: url, api_key...
};

/// Resolve the active backend and, for a local one, its .gguf file.
ActiveBackend resolveActiveBackend(ConfigManager *config);

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

/// A chat-completion backend: the same PromptPayload goes to a local
/// llama.cpp model or to a remote OpenAI-compatible endpoint.
///
/// Used synchronously from a worker thread: generate() returns when the text
/// is complete.  cancel() may be called meanwhile (from a slot run by the
/// event loop generate() pumps) to abort a remote request; a local
/// generation is stopped through `cancelFlag`.
class ChatBackend {
public:
  virtual ~ChatBackend() = default;

  virtual bool isLocal() const = 0;

  /// Generate a completion.  On cancellation the partial (possibly empty)
  /// text is returned: callers check their own cancel flag first.
  virtual std::expected<QString, GenerationError>
  generate(const PromptPayload &payload, const GenerationOptions &options,
           bool *cancelFlag) = 0;

  /// Abort an in-flight remote request.  No-op for a local backend.
  virtual void cancel() {}

  /// Free the model memory (local) once the run is over.
  virtual void release() {}
};

/// The backend matching `active`.
std::unique_ptr<ChatBackend> makeChatBackend(const ActiveBackend &active);
