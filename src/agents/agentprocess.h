#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include "agentconnection.h"

namespace Agents {

/// Whether an agent can start on this computer.
struct Availability {
  bool installed = false;      ///< its program is found (PATH or absolute path)
  QStringList missingRuntimes; ///< required runtimes not found ("node"...)
  bool ok() const { return installed && missingRuntimes.isEmpty(); }
};

/// Look for the program of `profile` and the runtimes it requires.
Availability availability(const Profile &profile);

/// Name and download page of a runtime ("node" -> Node.js).
QString runtimeName(const QString &runtime);
QString runtimeUrl(const QString &runtime);

/// The message when `profile` could not start (`processError`): what is
/// missing and how to install it.
QString startFailureMessage(const Profile &profile, const QString &processError);

/// An agent process that speaks JSON, one object per line, on stdin/stdout
/// (ACP, Claude's stream-json).  stderr is logged and its last
/// lines kept for error messages.  Shared by the connectors.
class AgentProcess : public QObject {
  Q_OBJECT

public:
  explicit AgentProcess(QObject *parent = nullptr);
  ~AgentProcess() override;

  /// Program and arguments of a command line ("qwen --acp", a path with
  /// spaces in quotes).  On Windows a .cmd/.bat program (npx...) is run
  /// through cmd.exe, as CreateProcess cannot start it.
  static QStringList splitCommand(const QString &commandLine);

  /// Start `commandLine` followed by `extraArgs`, in `workDir`, with `env`
  /// added to the environment.  False if it could not start (lastError()).
  bool start(const QString &commandLine, const QStringList &extraArgs,
             const QString &workDir, const QMap<QString, QString> &env = {});
  /// Write one JSON object and a newline.
  void write(const QJsonObject &message);
  /// Close stdin, wait `graceMs` for the process to exit, then terminate
  /// it, then kill it.
  void stop(int graceMs = 2000);
  /// Kill at once (a turn that does not stop).
  void kill();
  bool isRunning() const;
  QString lastError() const { return m_lastError; }
  /// The last lines written on stderr.
  QString stderrTail() const { return m_stderrTail.join('\n'); }

signals:
  void message(const QJsonObject &message);
  /// The process ended; `reason` for the user.  Not emitted after stop().
  void exited(const QString &reason);

private:
  void readStdout();

  QProcess m_process;
  QByteArray m_buffer;
  QString m_lastError;
  QStringList m_stderrTail;
  bool m_stopping = false;
};

} // namespace Agents
