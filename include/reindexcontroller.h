#pragma once

#include <QObject>
#include <atomic>

class DatabaseManager;
class EngineController;
class QThread;
class SqliteVecManager;

/// Runs the re-embedding recorded in the active project (see Reindexer) on
/// a worker thread, with its own connection: at startup, after a project
/// switch, and after an ingestion (it pauses during one: both use the
/// embedding model).  Exposed to QML as reindexController for the progress
/// in the status bar.  Failures go to the Notifier.
class ReindexController : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool running READ running NOTIFY progressChanged)
  Q_PROPERTY(int done READ done NOTIFY progressChanged)
  Q_PROPERTY(int total READ total NOTIFY progressChanged)

public:
  ReindexController(DatabaseManager *database, EngineController *engine,
                    SqliteVecManager &embeddings, QObject *parent = nullptr);
  /// Stops the work (at the end of the running batch) and waits for it.
  ~ReindexController() override;

  bool running() const { return m_running; }
  int done() const { return m_done; }
  int total() const { return m_total; }

  /// Start (or resume) the work of the active project, if any.
  void start();

signals:
  void progressChanged();
  /// Every chunk of the project was embedded again.
  void finished(int total);

private:
  /// Worker thread: the whole job for the database at `path`.
  void run(const QString &path);
  void stop(bool restart);
  /// Stop and wait until the worker has closed its connection.
  void stopAndWait();
  void setProgress(bool running, int done, int total);

  DatabaseManager *m_database;
  SqliteVecManager &m_embeddings;
  QThread *m_thread = nullptr;
  std::atomic<bool> m_cancel{false};
  bool m_restart = false;   ///< start again once the running job has stopped
  bool m_ingesting = false; ///< paused during an ingestion
  bool m_running = false;
  int m_done = 0;
  int m_total = 0;
};
