#include "agentregistry.h"

#include "apppaths.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace Agents {

const Registry &Registry::instance() {
  static const Registry registry = []() {
    Registry r;
    for (auto &c : builtinConnectors())
      r.addConnector(std::move(c));
    for (const QString &bad : r.loadProfiles(":/agents/profiles"))
      qWarning() << "[Agents] built-in profile not read:" << bad;
    for (const QString &bad : r.loadProfiles(AppPaths::dataDir() + "/Agents/profiles"))
      qWarning() << "[Agents] user profile not read:" << bad;
    for (Profile &p : r.m_profiles) {
      p.stateDir = AppPaths::dataDir() + "/Agents/state/" + p.id;
      p.guardCommand = {QCoreApplication::applicationFilePath(), QStringLiteral("--agent-guard")};
    }
    qInfo() << "[Agents]" << r.m_connectors.size() << "connectors,"
            << r.m_profiles.size() << "agent profiles";
    return r;
  }();
  return registry;
}

void Registry::addConnector(std::unique_ptr<Connector> connector) {
  if (connector)
    m_connectors.push_back(std::move(connector));
}

void Registry::addProfile(const Profile &profile) {
  for (Profile &p : m_profiles)
    if (p.id == profile.id) {
      p = profile;
      return;
    }
  m_profiles.append(profile);
}

QStringList Registry::loadProfiles(const QString &dir) {
  QStringList failed;
  const QDir folder(dir);
  for (const QString &name : folder.entryList({"*.json"}, QDir::Files, QDir::Name)) {
    QFile file(folder.filePath(name));
    QJsonParseError error;
    const QJsonDocument doc = file.open(QIODevice::ReadOnly)
                                  ? QJsonDocument::fromJson(file.readAll(), &error)
                                  : QJsonDocument();
    auto profile = doc.isObject() ? parseProfile(doc.object())
                                  : std::unexpected(error.errorString());
    if (!profile) {
      failed.append(name + ": " + profile.error());
      continue;
    }
    addProfile(*profile);
  }
  return failed;
}

const Connector *Registry::connector(const QString &id) const {
  for (const auto &c : m_connectors)
    if (c->id() == id)
      return c.get();
  return nullptr;
}

std::optional<Profile> Registry::profile(const QString &id) const {
  for (const Profile &p : m_profiles)
    if (p.id == id)
      return p;
  return std::nullopt;
}

Connection *Registry::create(const Profile &profile, QObject *parent) const {
  const Connector *c = connector(profile.connector);
  return c ? c->create(profile, parent) : nullptr;
}

std::expected<Profile, QString> Registry::parseProfile(const QJsonObject &json) {
  Profile p;
  p.id = json.value("id").toString().trimmed();
  p.connector = json.value("connector").toString().trimmed();
  p.command = json.value("command").toString().trimmed();
  if (p.id.isEmpty() || p.connector.isEmpty() || p.command.isEmpty())
    return std::unexpected(QStringLiteral("\"id\", \"connector\" and \"command\" are required"));
  p.name = json.value("name").toString(p.id);
  p.verified = json.value("verified").toBool(false);
  p.installHint = json.value("install").toString();
  for (const QJsonValue &r : json.value("requires").toArray())
    p.runtimes.append(r.toString());
  p.loginHint = json.value("login").toString();
  for (const QJsonValue &m : json.value("models").toArray())
    p.models.append(m.toString());
  const QJsonObject env = json.value("env").toObject();
  for (auto it = env.begin(); it != env.end(); ++it)
    p.env.insert(it.key(), it.value().toString());
  p.options = json.value("options").toObject();
  return p;
}

} // namespace Agents
