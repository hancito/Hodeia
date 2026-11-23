#include "agentchatcontroller.h"
#include "prompts.h"

#include "agentbackend.h"
#include "agentregistry.h"
#include "citationformat.h"
#include "configmanager.h"
#include "database.h"
#include "libraryrepository.h"
#include "metadataresolver.h"
#include "zoterorepository.h"

#include <QCoreApplication>
#include <QDebug>

namespace {

/// HodeIA's MCP tools (see McpLibrary::registerTools): the agent may use them
/// without asking.  save_summary is the only write; it never replaces a
/// summary unless asked to.
const QStringList kTools{"search_library", "list_papers", "get_summary", "get_page",
                         "get_citations", "save_summary"};
constexpr const char *kServer = "hodeia";
/// Title of a new conversation: the start of its first message.
constexpr int kTitleLength = 60;
/// The streamed answer is shown again at most this often.
constexpr int kLiveIntervalMs = 120;

} // namespace

AgentChatController::AgentChatController(ConfigManager *config, DatabaseManager *database,
                                         QObject *parent)
    : QObject(parent), m_config(config), m_database(database) {
  connect(m_database, &DatabaseManager::databaseSwitched, this, [this]() {
    dropConnection();
    m_currentId = 0;
    setBusy(false);
    emit currentChanged();
    emit messagesChanged();
    emit conversationsChanged();
  });
  connect(m_config, &ConfigManager::backendListChanged, this,
          &AgentChatController::agentBackendsChanged);
  connect(m_config, &ConfigManager::activeBackendChanged, this,
          &AgentChatController::agentBackendsChanged);
  connect(m_config, &ConfigManager::activeModelChanged, this,
          &AgentChatController::agentBackendsChanged);
  m_liveTimer.setSingleShot(true);
  m_liveTimer.setInterval(kLiveIntervalMs);
  connect(&m_liveTimer, &QTimer::timeout, this, &AgentChatController::renderLive);
}

void AgentChatController::renderLive() {
  // Marks still being written ("[@95f8") stay as they are until complete;
  // the Sources come with the stored answer, once it is complete.
  const QString rendered =
      CitationFormat::resolvePaperMarks(m_liveText, m_liveLabels, /*withSources=*/false);
  if (rendered != m_liveRendered) {
    m_liveRendered = rendered;
    emit liveTextChanged();
  }
}

void AgentChatController::clearLive() {
  m_liveTimer.stop();
  m_liveText.clear();
  if (!m_liveRendered.isEmpty()) {
    m_liveRendered.clear();
    emit liveTextChanged();
  }
}

AgentChatController::~AgentChatController() { dropConnection(); }

/* ── State for QML ─────────────────────────────────────────────── */

QVariantList AgentChatController::conversations() const {
  QVariantList list;
  if (!m_database->isOpen())
    return list;
  for (const ConversationRow &c : ConversationRepository(m_database->db()).conversations())
    list.append(QVariantMap{{"id", c.id},
                            {"title", c.title},
                            {"backend", c.backend},
                            {"model", c.model},
                            {"updatedAt", c.updatedAt.toLocalTime()},
                            {"messageCount", c.messageCount}});
  return list;
}

QHash<QString, PaperLabel> AgentChatController::paperLabels() const {
  QHash<QString, PaperLabel> labels;
  const QHash<QString, ZoteroMeta> zotero =
      m_config->zoteroPriority() ? ZoteroRepository(m_database->db()).metaByHash()
                                 : QHash<QString, ZoteroMeta>();
  for (const IndexedPaper &p : LibraryRepository(m_database->db()).indexedPapers()) {
    PaperLabel l = MetadataResolver::paperLabel(p.paperHash, p.cleanTitle, p.citation);
    if (const auto it = zotero.constFind(p.paperHash); it != zotero.constEnd())
      l = MetadataResolver::withZotero(l, *it);
    labels.insert(p.paperHash, l);
  }
  return labels;
}

QVariantList AgentChatController::messages() const {
  QVariantList list;
  if (m_currentId == 0 || !m_database->isOpen())
    return list;
  const QList<ConversationMessage> stored =
      ConversationRepository(m_database->db()).messages(m_currentId);
  QHash<QString, PaperLabel> labels;
  bool labelsRead = false;
  for (const ConversationMessage &m : stored) {
    QString text = m.content;
    if (m.role == ConversationMessage::Role::Assistant) {
      if (!labelsRead) {
        labels = paperLabels();
        labelsRead = true;
      }
      text = CitationFormat::resolvePaperMarks(text, labels);
    }
    list.append(QVariantMap{{"role", conversationRoleName(m.role)}, {"text", text}});
  }
  return list;
}

QVariantList AgentChatController::agentBackends() const {
  QVariantList list;
  const Agents::Registry &registry = Agents::Registry::instance();
  for (const QVariant &v : m_config->backendList()) {
    const QVariantMap b = v.toMap();
    if (b.value("api").toString() != "agent")
      continue;
    const auto profile = AgentBackend::profileOf(b);
    const Agents::Connector *c = profile ? registry.connector(profile->connector) : nullptr;
    if (!c || !c->supportsMcp())
      continue;
    const QString name = b.value("name").toString();
    list.append(QVariantMap{
        {"name", name},
        {"agent", profile->name},
        {"model", name == m_config->activeBackend() ? m_config->activeModel() : QString()}});
  }
  return list;
}

bool AgentChatController::agentAvailable() const {
  const QString active = m_config->activeBackend();
  for (const QVariant &b : agentBackends())
    if (b.toMap().value("name").toString() == active)
      return true;
  return false;
}

void AgentChatController::setBusy(bool busy) {
  if (m_busy != busy) {
    m_busy = busy;
    emit busyChanged();
  }
  if (!busy)
    setStatus({});
}

void AgentChatController::setStatus(const QString &status) {
  if (m_status != status) {
    m_status = status;
    emit statusChanged();
  }
}

/* ── Conversations ─────────────────────────────────────────────── */

void AgentChatController::newConversation(const QString &backend, const QString &model) {
  if (m_busy)
    return;
  dropConnection();
  m_currentId = 0;
  m_backend = backend;
  m_model = model;
  emit currentChanged();
  emit messagesChanged();
}

void AgentChatController::openConversation(int id) {
  if (m_busy || id == m_currentId || !m_database->isOpen())
    return;
  const auto c = ConversationRepository(m_database->db()).conversation(id);
  if (!c)
    return;
  dropConnection();
  m_currentId = id;
  m_backend = c->backend;
  m_model = c->model;
  emit currentChanged();
  emit messagesChanged();
}

QString AgentChatController::rename(int id, const QString &title) {
  if (auto r = ConversationRepository(m_database->db()).rename(id, title.trimmed()); !r)
    return r.error();
  emit conversationsChanged();
  return {};
}

QString AgentChatController::remove(const QVariantList &ids) {
  QList<int> list;
  for (const QVariant &v : ids)
    list.append(v.toInt());
  if (list.contains(m_currentId)) {
    if (m_busy)
      stop();
    dropConnection();
    m_currentId = 0;
    emit currentChanged();
    emit messagesChanged();
  }
  auto r = ConversationRepository(m_database->db()).remove(list);
  emit conversationsChanged();
  return r ? QString() : r.error();
}

void AgentChatController::closeAgent() {
  if (m_busy)
    stop();
  dropConnection();
  setBusy(false);
}

void AgentChatController::dropConnection() {
  clearLive();
  if (m_connection) {
    m_connection->disconnect(this);
    m_connection->close();
    m_connection->deleteLater();
  }
  m_connection = nullptr;
  m_connectionFor = 0;
  m_pendingText.clear();
}

/* ── A turn ────────────────────────────────────────────────────── */

QString AgentChatController::systemPrompt(const QString &language) {
  return Prompts::agentSystem(language);
}

Agents::McpServer AgentChatController::hodeiaMcpServer() {
  return {kServer, QCoreApplication::applicationFilePath(), {"--mcp"}, {}};
}

QString AgentChatController::transcript(const QList<ConversationMessage> &messages) {
  QStringList turns;
  for (const ConversationMessage &m : messages) {
    if (m.role == ConversationMessage::Role::Error)
      continue;
    turns.append(m.role == ConversationMessage::Role::User
                     ? Prompts::agentUserTurn(m.content)
                     : Prompts::agentAssistantTurn(m.content));
  }
  return Prompts::agentTranscript(turns);
}

void AgentChatController::addMessage(ConversationMessage::Role role, const QString &content) {
  if (auto r = ConversationRepository(m_database->db()).addMessage(m_currentId, role, content);
      !r)
    emit errorOccurred(tr("The message could not be saved: %1").arg(r.error()));
  emit messagesChanged();
  emit conversationsChanged();
}

void AgentChatController::send(const QString &text) {
  const QString message = text.trimmed();
  if (message.isEmpty() || m_busy || !m_database->isOpen())
    return;
  if (m_backend.isEmpty()) {
    const QVariantList backends = agentBackends();
    if (backends.isEmpty()) {
      emit errorOccurred(tr("No agent can search the library: add Claude Code or another "
                            "agent in the Backends window."));
      return;
    }
    m_backend = backends.first().toMap().value("name").toString();
    m_model = backends.first().toMap().value("model").toString();
    emit currentChanged();
  }

  ConversationRepository repo(m_database->db());
  if (m_currentId == 0) {
    const QString title = message.left(kTitleLength).simplified() +
                          (message.size() > kTitleLength ? QStringLiteral("…") : QString());
    auto id = repo.create(title, m_backend, m_model);
    if (!id) {
      emit errorOccurred(tr("The conversation could not be created: %1").arg(id.error()));
      return;
    }
    m_currentId = *id;
    emit currentChanged();
  }
  addMessage(ConversationMessage::Role::User, message);
  m_stopped = false;
  setBusy(true);
  startTurn(message);
}

void AgentChatController::startTurn(const QString &text) {
  if (m_connection && m_connectionFor == m_currentId) {
    QString prompt = text;
    if (m_needsTranscript) {
      // The agent could not resume the conversation: give it the story.
      QList<ConversationMessage> history =
          ConversationRepository(m_database->db()).messages(m_currentId);
      if (!history.isEmpty())
        history.removeLast(); // the message being sent
      prompt = transcript(history) + text;
      m_needsTranscript = false;
    }
    setStatus(tr("The agent is working..."));
    clearLive();
    m_liveLabels = paperLabels();
    m_connection->prompt({Agents::PromptPart::textPart(prompt)});
    return;
  }

  // Open the agent for this conversation first.
  dropConnection();
  const QVariantMap config = m_config->backendConfig(m_backend);
  const auto profile = AgentBackend::profileOf(config);
  Agents::Connection *c =
      profile ? Agents::Registry::instance().create(*profile, this) : nullptr;
  if (!c) {
    addMessage(ConversationMessage::Role::Error,
               tr("The agent backend \"%1\" is not available: check the Backends window.")
                   .arg(m_backend));
    setBusy(false);
    return;
  }
  m_connection = c;
  m_connectionFor = m_currentId;
  m_pendingText = text;
  connect(c, &Agents::Connection::opened, this, &AgentChatController::onOpened);
  connect(c, &Agents::Connection::agentEvent, this, &AgentChatController::onEvent);
  connect(c, &Agents::Connection::turnFinished, this, &AgentChatController::onTurnFinished);
  connect(c, &Agents::Connection::failed, this, &AgentChatController::onFailed);

  const auto stored = ConversationRepository(m_database->db()).conversation(m_currentId);
  Agents::SessionOptions options;
  options.workDir = AgentBackend::workDir();
  options.systemPrompt = systemPrompt(m_config->outputLanguage());
  options.model = m_model;
  options.tools = false;
  options.mcpServers = {hodeiaMcpServer()};
  for (const QString &tool : kTools)
    options.allowedTools.append(QString(kServer) + "/" + tool);
  options.resumeSessionId = stored ? stored->agentSession : QString();
  options.stream = true;
  m_resuming = !options.resumeSessionId.isEmpty();
  setStatus(tr("Starting the agent..."));
  c->open(options);
}

void AgentChatController::onOpened() {
  if (!m_connection)
    return;
  const auto stored = ConversationRepository(m_database->db()).conversation(m_connectionFor);
  // Not resumed (no stored session, or the agent cannot resume it): it
  // needs the conversation so far, when there is one.
  m_needsTranscript = stored && stored->messageCount > 1 && !m_connection->resumed();
  const QString text = std::exchange(m_pendingText, QString());
  m_firstTurn = m_resuming ? text : QString();
  startTurn(text);
}

void AgentChatController::retryWithoutResume(const QString &text, const QString &why) {
  qInfo() << "[AgentChat] session not resumed:" << why;
  ConversationRepository repo(m_database->db());
  if (const auto stored = repo.conversation(m_connectionFor))
    (void)repo.setAgent(stored->id, stored->backend, stored->model, QString());
  dropConnection();
  m_resuming = false;
  m_firstTurn.clear();
  startTurn(text);
}

void AgentChatController::onEvent(const Agents::Event &event) {
  switch (event.kind) {
  case Agents::Event::Kind::ToolCall: {
    const QString tool = event.text;
    if (tool.contains("search_library"))
      setStatus(tr("Searching the library..."));
    else if (tool.contains("get_page"))
      setStatus(tr("Reading a page..."));
    else if (tool.contains("get_summary"))
      setStatus(tr("Reading a summary..."));
    else if (tool.contains("list_papers"))
      setStatus(tr("Listing the papers..."));
    else if (tool.contains("get_citations"))
      setStatus(tr("Looking at the citations..."));
    else if (tool.contains("save_summary"))
      setStatus(tr("Saving a summary..."));
    else if (!tool.isEmpty())
      setStatus(tr("Using %1...").arg(tool));
    break;
  }
  case Agents::Event::Kind::Thought:
    setStatus(tr("Thinking..."));
    break;
  case Agents::Event::Kind::Text:
    setStatus(tr("Writing the answer..."));
    m_liveText += event.text;
    if (!m_liveTimer.isActive())
      m_liveTimer.start();
    break;
  case Agents::Event::Kind::Notice:
    if (!event.text.isEmpty())
      setStatus(event.text);
    break;
  }
}

void AgentChatController::onTurnFinished(const Agents::TurnResult &result) {
  using Stop = Agents::TurnResult::Stop;
  clearLive();
  // Some agents (Claude Code) only fail to resume at the first message.
  if (result.stop == Stop::Error && !m_firstTurn.isEmpty() && !m_stopped) {
    retryWithoutResume(m_firstTurn, result.error);
    return;
  }
  m_firstTurn.clear();
  m_resuming = false;
  // The agent's session id, to resume this conversation later.
  if (m_connection && !m_connection->sessionId().isEmpty()) {
    ConversationRepository repo(m_database->db());
    const auto stored = repo.conversation(m_connectionFor);
    if (stored && stored->agentSession != m_connection->sessionId())
      (void)repo.setAgent(m_connectionFor, stored->backend, stored->model,
                          m_connection->sessionId());
  }
  if (result.stop == Stop::Error || result.stop == Stop::Refusal) {
    if (!result.text.trimmed().isEmpty())
      addMessage(ConversationMessage::Role::Assistant, result.text);
    addMessage(ConversationMessage::Role::Error,
               result.error.isEmpty() ? tr("The agent failed.") : result.error);
  } else if (!result.text.trimmed().isEmpty()) {
    QString text = result.text;
    if (result.stop == Stop::Cancelled || m_stopped)
      text += "\n\n*" + tr("(stopped)") + "*";
    addMessage(ConversationMessage::Role::Assistant, text);
  } else if (result.stop == Stop::Cancelled || m_stopped) {
    addMessage(ConversationMessage::Role::Error, tr("Stopped before answering."));
  }
  setBusy(false);
}

void AgentChatController::onFailed(const QString &error) {
  clearLive();
  // The stored session could not be resumed: forget it and start a new one
  // (it gets the conversation so far).
  if (m_resuming && m_busy && !(m_pendingText.isEmpty() && m_firstTurn.isEmpty())) {
    retryWithoutResume(m_pendingText.isEmpty() ? m_firstTurn : m_pendingText, error);
    return;
  }
  // The agent could not start, or stopped: the next message starts it again.
  if (m_busy) {
    addMessage(ConversationMessage::Role::Error, error);
    setBusy(false);
  }
  if (m_connection) {
    m_connection->disconnect(this);
    m_connection->deleteLater();
  }
  m_connection = nullptr;
  m_connectionFor = 0;
}

void AgentChatController::stop() {
  if (!m_busy)
    return;
  m_stopped = true;
  if (!m_pendingText.isEmpty() || !m_connection) {
    // Still starting the agent: nothing was asked yet.
    dropConnection();
    addMessage(ConversationMessage::Role::Error, tr("Stopped before answering."));
    setBusy(false);
    return;
  }
  setStatus(tr("Stopping..."));
  m_connection->cancel();
}
