#include "agentsync.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <optional>
#include <QTimer>

namespace Agents {

std::expected<void, QString> openSync(Connection &connection,
                                      const SessionOptions &options,
                                      int timeoutMs) {
  QEventLoop loop;
  std::optional<QString> error;
  bool done = false;
  const auto ok = QObject::connect(&connection, &Connection::opened, &loop, [&]() {
    done = true;
    loop.quit();
  });
  const auto ko = QObject::connect(&connection, &Connection::failed, &loop,
                                   [&](const QString &e) {
                                     done = true;
                                     error = e;
                                     loop.quit();
                                   });
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
    if (!done)
      error = QStringLiteral("timeout");
    done = true;
    loop.quit();
  });
  if (timeoutMs > 0)
    timer.start(timeoutMs);
  connection.open(options);
  if (!done)
    loop.exec(QEventLoop::ExcludeUserInputEvents);
  QObject::disconnect(ok);
  QObject::disconnect(ko);
  if (error)
    return std::unexpected(*error == "timeout"
                               ? QCoreApplication::translate("Agents", "The agent did not answer.")
                               : *error);
  return {};
}

TurnResult promptSync(Connection &connection, const Prompt &prompt, int timeoutMs) {
  QEventLoop loop;
  std::optional<TurnResult> result;
  const auto finished = QObject::connect(
      &connection, &Connection::turnFinished, &loop, [&](const TurnResult &r) {
        if (!result)
          result = r;
        loop.quit();
      });
  const auto died = QObject::connect(&connection, &Connection::failed, &loop,
                                     [&](const QString &e) {
                                       if (!result)
                                         result = TurnResult{TurnResult::Stop::Error, {}, e};
                                       loop.quit();
                                     });
  QTimer timer;
  timer.setSingleShot(true);
  bool timedOut = false;
  QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
    timedOut = true;
    connection.cancel(); // turnFinished(Cancelled) follows
    QTimer::singleShot(5000, &loop, &QEventLoop::quit); // in case it does not
  });
  if (timeoutMs > 0)
    timer.start(timeoutMs);
  connection.prompt(prompt);
  if (!result)
    loop.exec(QEventLoop::ExcludeUserInputEvents);
  QObject::disconnect(finished);
  QObject::disconnect(died);
  TurnResult r = result.value_or(TurnResult{TurnResult::Stop::Error, {}, {}});
  if (timedOut) {
    r.stop = TurnResult::Stop::Error;
    r.error = QStringLiteral("timeout");
  }
  return r;
}

} // namespace Agents
