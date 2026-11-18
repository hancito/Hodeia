#include "mcpstdio.h"

#include "mcpserver.h"

#include <QDebug>
#include <QMetaObject>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace {

/// Read some bytes from stdin; 0 at end of input or on error.
qint64 readStdin(char *buffer, qint64 size) {
#ifdef Q_OS_WIN
  DWORD got = 0;
  if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), buffer, DWORD(size), &got,
                nullptr))
    return 0; // ERROR_BROKEN_PIPE: the client closed its end
  return qint64(got);
#else
  while (true) {
    const ssize_t got = ::read(STDIN_FILENO, buffer, size_t(size));
    if (got < 0 && errno == EINTR)
      continue;
    return got > 0 ? qint64(got) : 0;
  }
#endif
}

/// Write all of `data` to stdout.
bool writeStdout(const QByteArray &data) {
  const char *p = data.constData();
  qint64 left = data.size();
  while (left > 0) {
#ifdef Q_OS_WIN
    DWORD done = 0;
    if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), p, DWORD(left), &done,
                   nullptr))
      return false;
    const qint64 n = qint64(done);
#else
    const ssize_t n = ::write(STDOUT_FILENO, p, size_t(left));
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      return false;
#endif
    p += n;
    left -= n;
  }
  return true;
}

} // namespace

McpStdioTransport::McpStdioTransport(McpServer &server, QObject *parent)
    : QObject(parent), m_server(server) {}

McpStdioTransport::~McpStdioTransport() {
  // The reader is blocked in a read until stdin closes; by then it has
  // finished.  Never block the exit on it.
  if (m_reader && m_reader->isRunning())
    m_reader->terminate();
}

void McpStdioTransport::start() {
  m_reader = QThread::create([this]() {
    QByteArray pending;
    char buffer[64 * 1024];
    while (true) {
      const qint64 got = readStdin(buffer, sizeof(buffer));
      if (got <= 0)
        break;
      pending.append(buffer, got);
      qsizetype nl;
      while ((nl = pending.indexOf('\n')) >= 0) {
        QByteArray line = pending.left(nl);
        pending.remove(0, nl + 1);
        if (line.endsWith('\r'))
          line.chop(1);
        if (!line.trimmed().isEmpty())
          QMetaObject::invokeMethod(this, "onLine", Qt::QueuedConnection,
                                    Q_ARG(QByteArray, line));
      }
    }
    qInfo() << "[MCP] stdin closed";
    QMetaObject::invokeMethod(this, "closed", Qt::QueuedConnection);
  });
  m_reader->setParent(this);
  m_reader->start();
}

void McpStdioTransport::onLine(const QByteArray &line) {
  const QByteArray response = m_server.handleMessage(line);
  if (!response.isEmpty())
    writeLine(response);
}

void McpStdioTransport::writeLine(const QByteArray &line) {
  // One message per line: QJsonDocument::Compact never emits a newline.
  if (!writeStdout(line + '\n'))
    qWarning() << "[MCP] write to stdout failed";
}
