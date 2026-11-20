#pragma once

#include "agentconnection.h"
#include "chatbackend.h"

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <expected>
#include <memory>
#include <optional>

/// The "Agent" backend: questions answered by a coding agent (Claude Code,
/// Qwen Code, Pi...) through the agent connectors (src/agents), with the
/// user's own login to that agent.  Backend config: {"api": "agent",
/// "agent": <profile id>, "url": <optional command that replaces the
/// profile's>}; the model is one of the agent's.
///
/// Classic mode (the Query window and the summaries, through ChatBackend):
/// one new session per generation, with HodeIA's system prompt and, when
/// the agent allows it, no tool and none of the user's settings.
namespace AgentBackend {

/// The agent profiles, for QML: {id, name, command, verified, installed,
/// missingRuntimes ("Node.js"), install}; the ones that can start first.
QVariantList profiles();

/// The profile id that a backend name designates ("Claude Code" -> claude),
/// for backends saved without their "agent" key; empty if none.
QString guessAgent(const QString &backendName);

/// The profile of a backend config, with its command override.
std::optional<Agents::Profile> profileOf(const QVariantMap &config);

/// Folder the agents run in (created if needed): empty of the user's files.
QString workDir();

/// The models of the agent of `config` (may start it: a few seconds).
std::expected<QStringList, QString> listModels(const QVariantMap &config);

/// The prompt of a payload: the same layout as the Anthropic backend
/// (instructions, then each page's image before its text).
Agents::Prompt prompt(const PromptPayload &payload);

std::unique_ptr<ChatBackend> make(const QString &model, const QVariantMap &config);

} // namespace AgentBackend
