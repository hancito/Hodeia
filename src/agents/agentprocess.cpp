#include "agentprocess.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace Agents {

namespace {
constexpr int kStderrLines = 20;
} // namespace

QString runtimeName(const QString &runtime) {
  if (runtime == "node")
    return QStringLiteral("Node.js");
  if (runtime == "uv")
    return QStringLiteral("uv");
  return runtime;
}

QString runtimeUrl(const QString &runtime) {
  if (runtime == "node")
    return QStringLiteral("https://nodejs.org");
  if (runtime == "uv")
    return QStringLiteral("https://docs.astral.sh/uv");
  return {};
}

namespace {
bool programFound(const QString &program) {
  if (program.isEmpty())
    return false;
  const QFileInfo info(program);
  if (info.isAbsolute() || program.contains('/') || program.contains('\\'))
    return info.exists();
  return !QStandardPaths::findExecutable(program).isEmpty();
}
} // namespace

Availability availability(const Profile &profile) {
  Availability a;
  const QStringList parts = QProcess::splitCommand(profile.command.trimmed());
  a.installed = !parts.isEmpty() && programFound(parts.first());
  for (const QString &runtime : profile.runtimes) {
    const QString program = runtime == "uv" ? QStringLiteral("uvx") : runtime;
    if (!programFound(program))
      a.missingRuntimes.append(runtime);
  }
  return a;
}

QString startFailureMessage(const Profile &profile, const QString &processError) {
  const Availability a = availability(profile);
  QStringList lines;
  if (!a.missingRuntimes.isEmpty()) {
    QStringList needed;
    for (const QString &r : a.missingRuntimes)
      needed.append(QString("%1 (%2)").arg(runtimeName(r), runtimeUrl(r)));
    lines.append(QCoreApplication::translate("Agents::AgentProcess", "%1 needs %2.")
                     .arg(profile.name, needed.join(", ")));
  } else if (!a.installed) {
    lines.append(QCoreApplication::translate("Agents::AgentProcess",
                                             "%1 is not installed on this computer.")
                     .arg(profile.name));
  } else {
    lines.append(processError);
  }
  if (!profile.installHint.isEmpty())
    lines.append(QCoreApplication::translate("Agents::AgentProcess", "Install it with: %1")
                     .arg(profile.installHint));
  return lines.join('\n');
}

AgentProcess::AgentProcess(QObject *parent) : QObject(parent) {
  m_process.setProcessChannelMode(QProcess::SeparateChannels);
  connect(&m_process, &QProcess::readyReadStandardOutput, this,
          &AgentProcess::readStdout);
  connect(&m_process, &QProcess::readyReadStandardError, this, [this]() {
    const QString text = QString::fromUtf8(m_process.readAllStandardError());
    for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
      qDebug().noquote() << "[Agent stderr]" << line.left(500);
      m_stderrTail.append(line);
    }
    while (m_stderrTail.size() > kStderrLines)
      m_stderrTail.removeFirst();
  });
  connect(&m_process, &QProcess::finished, this,
          [this](int code, QProcess::ExitStatus status) {
            readStdout(); // what was written just before exiting
            if (m_stopping)
              return;
            emit exited(status == QProcess::CrashExit
                            ? QCoreApplication::translate(
                                  "Agents::AgentProcess",
                                  "the agent stopped unexpectedly")
                            : QCoreApplication::translate(
                                  "Agents::AgentProcess",
                                  "the agent exited (code %1)")
                                  .arg(code));
          });
}

AgentProcess::~AgentProcess() { stop(500); }

QStringList AgentProcess::splitCommand(const QString &commandLine) {
  QStringList parts = QProcess::splitCommand(commandLine.trimmed());
  if (parts.isEmpty())
    return parts;
#ifdef Q_OS_WIN
  QString program = parts.first();
  if (QFileInfo(program).suffix().isEmpty()) {
    const QString found = QStandardPaths::findExecutable(program);
    if (!found.isEmpty())
      program = found;
  }
  const QString suffix = QFileInfo(program).suffix().toLower();
  if (suffix == "cmd" || suffix == "bat") {
    parts[0] = program;
    parts.prepend("/c");
    parts.prepend("cmd.exe");
  }
#endif
  return parts;
}

bool AgentProcess::start(const QString &commandLine, const QStringList &extraArgs,
                         const QString &workDir, const QMap<QString, QString> &env) {
  const QStringList parts = splitCommand(commandLine);
  if (parts.isEmpty()) {
    m_lastError = QCoreApplication::translate("Agents::AgentProcess",
                                              "No command to start the agent.");
    return false;
  }
  m_lastError.clear();
  m_stderrTail.clear();
  m_buffer.clear();
  m_stopping = false;
  if (!env.isEmpty()) {
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (auto it = env.cbegin(); it != env.cend(); ++it)
      environment.insert(it.key(), it.value());
    m_process.setProcessEnvironment(environment);
  }
  m_process.setWorkingDirectory(workDir);
  m_process.start(parts.first(), parts.mid(1) + extraArgs);
  if (!m_process.waitForStarted(15000)) {
    m_lastError = QCoreApplication::translate("Agents::AgentProcess",
                                              "Cannot start \"%1\": %2")
                      .arg(parts.first(), m_process.errorString());
    return false;
  }
  qInfo() << "[Agent] started" << parts.first() << "with"
          << (parts.size() - 1 + extraArgs.size()) << "arguments";
  return true;
}

void AgentProcess::write(const QJsonObject &message) {
  if (!isRunning())
    return;
  QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact);
  line.append('\n');
  m_process.write(line);
}

void AgentProcess::stop(int graceMs) {
  if (m_process.state() == QProcess::NotRunning)
    return;
  m_stopping = true;
  m_process.closeWriteChannel();
  if (!m_process.waitForFinished(graceMs)) {
    m_process.terminate();
    if (!m_process.waitForFinished(1000))
      m_process.kill();
    m_process.waitForFinished(1000);
  }
}

void AgentProcess::kill() {
  if (m_process.state() == QProcess::NotRunning)
    return;
  m_stopping = true;
  m_process.kill();
  m_process.waitForFinished(1000);
}

bool AgentProcess::isRunning() const {
  return m_process.state() == QProcess::Running;
}

void AgentProcess::readStdout() {
  m_buffer.append(m_process.readAllStandardOutput());
  // Split on LF only (not on U+2028/2029, which JSON strings may hold).
  qsizetype newline;
  while ((newline = m_buffer.indexOf('\n')) >= 0) {
    const QByteArray line = m_buffer.left(newline).trimmed();
    m_buffer.remove(0, newline + 1);
    if (line.isEmpty())
      continue;
    const QJsonDocument doc = QJsonDocument::fromJson(line);
    if (!doc.isObject()) {
      qDebug() << "[Agent] not JSON:" << line.left(200);
      continue;
    }
    emit message(doc.object());
  }
}

} // namespace Agents
