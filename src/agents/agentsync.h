#pragma once

#include "agentconnection.h"

#include <expected>

namespace Agents {

/// Opening an agent: the first start through npx may download it.
inline constexpr int kOpenTimeoutMs = 120000;

/// Wait (event loop) for open(): nothing, or the error.  `timeoutMs` 0 = no
/// limit.  For callers on a worker thread (backend, model listing).
std::expected<void, QString> openSync(Connection &connection,
                                      const SessionOptions &options,
                                      int timeoutMs);

/// Wait (event loop) for one turn.  On timeout the turn is cancelled and the
/// result has Stop::Error with error "timeout".  cancel() may be called
/// meanwhile from a slot (the loop processes events).
TurnResult promptSync(Connection &connection, const Prompt &prompt, int timeoutMs);

} // namespace Agents
