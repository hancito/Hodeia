#pragma once

#include "agentconnection.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <expected>
#include <memory>
#include <optional>
#include <vector>

namespace Agents {

/// The connectors built into the app: one per folder of
/// src/agents/connectors, listed by CMake (builtinconnectors.cpp, generated).
/// Each folder <name> defines `hodeia_connector_<name>()`.
std::vector<std::unique_ptr<Connector>> builtinConnectors();

/// The connectors and the agent profiles.  Profiles come from the app
/// (resources :/agents/profiles/*.json) and from the user's folder
/// (<data>/Agents/profiles/*.json), which may add agents or override a
/// built-in one with the same id: adding an ACP agent needs no code.
class Registry {
public:
  /// Built-in connectors and profiles, then the user's profiles.  Built once,
  /// read-only afterwards (safe from any thread).
  static const Registry &instance();

  /// An empty registry (tests).
  Registry() = default;

  void addConnector(std::unique_ptr<Connector> connector);
  /// Add or replace (same id) a profile.
  void addProfile(const Profile &profile);
  /// Load every *.json of `dir`; returns the files it could not read.
  QStringList loadProfiles(const QString &dir);

  const Connector *connector(const QString &id) const;
  QList<Profile> profiles() const { return m_profiles; }
  std::optional<Profile> profile(const QString &id) const;

  /// A connection for `profile`; nullptr if its connector is unknown.
  Connection *create(const Profile &profile, QObject *parent = nullptr) const;

  /// Profile from its JSON form (see profiles/*.json); an error message if a
  /// required field (id, connector, command) is missing.
  static std::expected<Profile, QString> parseProfile(const QJsonObject &json);

private:
  std::vector<std::unique_ptr<Connector>> m_connectors;
  QList<Profile> m_profiles;
};

} // namespace Agents
