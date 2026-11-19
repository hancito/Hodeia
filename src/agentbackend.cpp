#include "agentbackend.h"

#include "agentprocess.h"
#include "agentregistry.h"
#include "agentsync.h"
#include "anthropicapi.h"
#include "apppaths.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>

namespace {


class AgentChatBackend final : public ChatBackend {
public:
  AgentChatBackend(const QString &model, const std::optional<Agents::Profile> &profile,
                   const QString &agentId)
      : m_model(model), m_profile(profile), m_agentId(agentId) {}

  BackendCaps caps() const override { return {}; }

  std::expected<QString, GenerationError>
  generate(const PromptPayload &payload, const GenerationOptions &options,
           bool * /*cancelFlag: cancel() is called instead*/) override {
    const auto fail = [](const QString &detail) {
      qWarning() << "[AgentBackend]" << detail;
      return std::unexpected(
          GenerationError{GenerationError::Kind::RequestFailed, detail});
    };
    if (!m_profile)
      return fail(QCoreApplication::translate("AgentBackend", "Unknown agent \"%1\": choose one in the Backends window.")
                      .arg(m_agentId));
    std::unique_ptr<Agents::Connection> connection(
        Agents::Registry::instance().create(*m_profile));
    if (!connection)
      return fail(QCoreApplication::translate("AgentBackend", "No connector \"%1\" for the agent %2.")
                      .arg(m_profile->connector, m_profile->name));
    m_connection = connection.get();
    m_cancelled = false;
    struct Reset {
      AgentChatBackend *self;
      ~Reset() { self->m_connection = nullptr; }
    } reset{this};

    QElapsedTimer timer;
    timer.start();
    Agents::SessionOptions session;
    session.workDir = AgentBackend::workDir();
    session.systemPrompt = payload.systemPrompt;
    session.model = m_model;
    session.tools = false;
    if (auto opened = Agents::openSync(*connection, session, Agents::kOpenTimeoutMs);
        !opened) {
      if (m_cancelled)
        return QString();
      return fail(opened.error());
    }
    qDebug() << "[AgentBackend]" << m_profile->id << "session ready in"
             << timer.elapsed() << "ms | model:" << (m_model.isEmpty() ? "default" : m_model);

    const int remaining =
        options.timeoutMs > 0 ? qMax(1, options.timeoutMs - int(timer.elapsed())) : 0;
    const Agents::TurnResult turn =
        Agents::promptSync(*connection, AgentBackend::prompt(payload), remaining);
    connection->close();
    qDebug() << "[AgentBackend] answer in" << timer.elapsed() << "ms |"
             << turn.text.size() << "chars | stop:" << int(turn.stop);

    if (turn.stop == Agents::TurnResult::Stop::Error && turn.error == "timeout")
      return std::unexpected(GenerationError{GenerationError::Kind::Timeout, {}});
    if (m_cancelled || turn.stop == Agents::TurnResult::Stop::Cancelled)
      return turn.text;
    switch (turn.stop) {
    case Agents::TurnResult::Stop::Error:
      return fail(turn.error.isEmpty() ? QCoreApplication::translate("AgentBackend", "The agent failed.") : turn.error);
    case Agents::TurnResult::Stop::Refusal:
      return fail(turn.error.isEmpty() ? QCoreApplication::translate("AgentBackend", "The agent declined the request.") : turn.error);
    default:
      return turn.text;
    }
  }

  void cancel() override {
    m_cancelled = true;
    if (m_connection)
      m_connection->cancel();
  }

private:
  QString m_model;
  std::optional<Agents::Profile> m_profile;
  QString m_agentId;
  Agents::Connection *m_connection = nullptr;
  bool m_cancelled = false;
};

} // namespace

namespace AgentBackend {

QVariantList profiles() {
  struct Entry {
    Agents::Profile profile;
    Agents::Availability availability;
  };
  QList<Entry> entries;
  for (const Agents::Profile &p : Agents::Registry::instance().profiles())
    entries.append({p, Agents::availability(p)});
  // The agents that can start first, then the tested ones.
  std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
    if (a.availability.ok() != b.availability.ok())
      return a.availability.ok();
    return a.profile.verified && !b.profile.verified;
  });
  QVariantList list;
  for (const Entry &e : entries) {
    QStringList runtimes;
    for (const QString &r : e.availability.missingRuntimes)
      runtimes.append(Agents::runtimeName(r));
    list.append(QVariantMap{{"id", e.profile.id},
                            {"name", e.profile.name},
                            {"command", e.profile.command},
                            {"verified", e.profile.verified},
                            {"installed", e.availability.installed},
                            {"missingRuntimes", runtimes.join(", ")},
                            {"install", e.profile.installHint}});
  }
  return list;
}

std::optional<Agents::Profile> profileOf(const QVariantMap &config) {
  auto profile = Agents::Registry::instance().profile(config.value("agent").toString());
  const QString command = config.value("url").toString().trimmed();
  if (profile && !command.isEmpty())
    profile->command = command;
  return profile;
}

QString workDir() {
  const QString dir = AppPaths::dataDir() + "/Agent";
  QDir().mkpath(dir);
  return dir;
}

std::expected<QStringList, QString> listModels(const QVariantMap &config) {
  const auto profile = profileOf(config);
  if (!profile)
    return std::unexpected(QCoreApplication::translate("AgentBackend", "Unknown agent \"%1\".").arg(config.value("agent").toString()));
  const Agents::Connector *connector =
      Agents::Registry::instance().connector(profile->connector);
  if (!connector)
    return std::unexpected(QCoreApplication::translate("AgentBackend", "No connector \"%1\".").arg(profile->connector));
  return connector->listModels(*profile);
}

Agents::Prompt prompt(const PromptPayload &payload) {
  // Reuse the Anthropic layout, then turn its blocks into prompt parts.
  const QJsonArray content = AnthropicApi::buildBody(payload, QString())
                                 .value("messages").toArray()
                                 .first().toObject()
                                 .value("content").toArray();
  Agents::Prompt parts;
  for (const QJsonValue &v : content) {
    const QJsonObject block = v.toObject();
    if (block.value("type").toString() == "image") {
      const QJsonObject source = block.value("source").toObject();
      parts.append(Agents::PromptPart::image(source.value("media_type").toString(),
                                             source.value("data").toString().toLatin1()));
    } else {
      parts.append(Agents::PromptPart::textPart(block.value("text").toString()));
    }
  }
  return parts;
}

std::unique_ptr<ChatBackend> make(const QString &model, const QVariantMap &config) {
  return std::make_unique<AgentChatBackend>(model, profileOf(config),
                                            config.value("agent").toString());
}

} // namespace AgentBackend
