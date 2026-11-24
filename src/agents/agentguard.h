#pragma once

#include <QJsonObject>
#include <QStringList>

namespace Agents {

/* HodeIA's tool guard: a hook an agent runs before each of its tool calls
   (Antigravity's PreToolUse, `Hodeia --agent-guard <allowed>`).  It reads
   the hook's JSON on stdin and answers allow or deny on stdout, so that the
   agent can only search the web and use HodeIA's own tools, whatever its
   user's settings allow. */

/// The decision for one hook input: {"decision": "allow"} when the tool
/// (toolCall.name, or tool_name) is in `allowed`, else a deny with the
/// reason.  An entry ending with '*' allows every name it begins.
QJsonObject guardDecision(const QJsonObject &hookInput, const QStringList &allowed);

/// The guard program: `allowed` is the comma-separated argument after
/// --agent-guard.  Reads stdin, writes the decision; returns the exit code.
int runGuard(const QString &allowed);

} // namespace Agents
