#pragma once

#include "queryworker.h"
#include <QObject>
#include <QString>
#include <QThread>
#include <QVariantMap>

class ConfigManager;
class DatabaseManager;

/// Thread-safe controller exposed to QML for RAG queries.
/// Spawns a worker thread, runs the QueryWorker, and forwards signals.
class QueryController : public QObject {
  Q_OBJECT

public:
  explicit QueryController(ConfigManager *config, DatabaseManager *database,
                           QObject *parent = nullptr);
  ~QueryController() override;

  /// Context budget info for the active backend, for the query UI:
  ///   remote      — true for a remote backend (no placement estimate)
  ///   autoTokens  — budget used when the setting is 0 ("auto")
  ///   maxTokens   — largest budget offered
  ///   placement   — "gpu", "split" (GPU + CPU), "cpu" or "" (remote/unknown)
  ///   truncated   — the requested budget won't fit; the prompt gets cut
  /// \param contextTokens  requested budget; 0 = auto
  Q_INVOKABLE QVariantMap contextInfo(int contextTokens) const;

public slots:
  /// \param contextTokens  prompt budget in tokens; 0 = auto
  void startQuery(const QString &queryText, int contextTokens);
  void stopQuery();

signals:
  void queryStarted();
  /// On failure @p error is the cause; on success it is empty, or a warning
  /// when the answer could not be saved in the history.
  void queryFinished(const QString &response, bool success,
                     const QString &error);
  void queryStopped();

private:
  QThread *m_thread = nullptr;
  QueryWorker *m_worker = nullptr;
  DatabaseManager *m_database = nullptr;
  ConfigManager *m_config = nullptr;
};
