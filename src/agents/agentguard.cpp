#include "agentguard.h"

#include <QFile>
#include <QJsonDocument>
#include <cstdio>

namespace Agents {

QJsonObject guardDecision(const QJsonObject &hookInput, const QStringList &allowed) {
  QString name = hookInput.value("toolCall").toObject().value("name").toString();
  if (name.isEmpty())
    name = hookInput.value("tool_name").toString();
  for (const QString &entry : allowed) {
    const bool prefix = entry.endsWith('*');
    if (!name.isEmpty() &&
        (prefix ? name.startsWith(entry.chopped(1)) : name == entry))
      return {{"decision", "allow"}};
  }
  return {{"decision", "deny"},
          {"reason", QStringLiteral("Blocked by HodeIA: in HodeIA the agent may only search "
                                    "the web and use the HodeIA library tools (not %1).")
                         .arg(name.isEmpty() ? QStringLiteral("this tool") : name)}};
}

int runGuard(const QString &allowed) {
  QFile in;
  if (!in.open(stdin, QIODevice::ReadOnly))
    return 1;
  const QJsonObject input = QJsonDocument::fromJson(in.readAll()).object();
  const QByteArray out =
      QJsonDocument(guardDecision(input, allowed.split(',', Qt::SkipEmptyParts)))
          .toJson(QJsonDocument::Compact);
  std::fwrite(out.constData(), 1, size_t(out.size()), stdout);
  std::fputc('\n', stdout);
  std::fflush(stdout);
  return 0;
}

} // namespace Agents
