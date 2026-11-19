#include "reindexcontroller.h"

#include "database.h"
#include "enginemanager.h"
#include "notifier.h"
#include "reindexer.h"
#include "sqlitedb.h"
#include "sqlitevec.h"

#include <QDebug>
#include <QThread>
#include <QTimer>

namespace {
// Chunks per batch: small enough that stopping (project switch, quit)
// waits about a second.
constexpr int kBatch = 32;
} // namespace

ReindexController::ReindexController(DatabaseManager *database,
                                     EngineController *engine,
                                     SqliteVecManager &embeddings,
                                     QObject *parent)
    : QObject(parent), m_database(database), m_embeddings(embeddings) {
  connect(m_database, &DatabaseManager::databaseSwitched, this,
          &ReindexController::start);
  // The project is being deleted: its file must be released now.
  connect(m_database, &DatabaseManager::databaseClosing, this,
          &ReindexController::stopAndWait);
  connect(engine, &EngineController::ingestionStarted, this, [this]() {
    m_ingesting = true;
    stop(false);
  });
  connect(engine, &EngineController::ingestionFinished, this, [this]() {
    m_ingesting = false;
    start();
  });
  // Not during startup itself: the window first.
  QTimer::singleShot(3000, this, &ReindexController::start);
}

ReindexController::~ReindexController() {
  if (m_thread) {
    m_cancel = true;
    m_thread->wait();
    delete m_thread;
  }
}

void ReindexController::stopAndWait() {
  if (!m_thread)
    return;
  stop(false);
  m_thread->wait(); // at most one batch; finished() then cleans up
}

void ReindexController::setProgress(bool running, int done, int total) {
  m_running = running;
  m_done = done;
  m_total = total;
  emit progressChanged();
}

void ReindexController::stop(bool restart) {
  if (!m_thread)
    return;
  m_restart = restart;
  m_cancel = true;
}

void ReindexController::start() {
  if (m_ingesting)
    return;
  if (m_thread) { // another project, or still the same: once it has stopped
    stop(true);
    return;
  }
  const QString path = m_database->databasePath();
  if (path.isEmpty() || !m_database->isOpen())
    return;
  // Nothing to do: no thread.
  if (!Reindexer::status(m_database->db()))
    return;

  m_cancel = false;
  m_restart = false;
  m_thread = QThread::create([this, path]() { run(path); });
  connect(m_thread, &QThread::finished, this, [this]() {
    m_thread->deleteLater();
    m_thread = nullptr;
    if (m_restart) {
      m_restart = false;
      start();
    } else if (m_running) {
      setProgress(false, m_done, m_total); // stopped before the end
    }
  });
  m_thread->start(QThread::LowPriority);
}

void ReindexController::run(const QString &path) {
  SqliteDb db;
  if (auto r = db.open(path); !r) {
    qWarning() << "[Reindex] cannot open" << path << r.error();
    return;
  }
  auto status = Reindexer::status(db);
  if (!status)
    return;
  int done = status->done;
  const int total = status->total;
  qInfo() << "[Reindex]" << path << ":" << done << "/" << total
          << "chunks already embedded again";
  QMetaObject::invokeMethod(this, [=, this]() { setProgress(true, done, total); });

  while (!m_cancel) {
    auto n = Reindexer::step(db, m_embeddings, kBatch);
    if (!n) {
      qWarning() << "[Reindex] failed:" << n.error();
      Notifier::post(QStringLiteral("reindex"), tr("Search Index"),
                     tr("The search index of the project could not be updated: "
                        "%1\nIt will be tried again at the next start.")
                         .arg(n.error()));
      return;
    }
    if (*n == 0) {
      qInfo() << "[Reindex] done:" << total << "chunks";
      QMetaObject::invokeMethod(this, [=, this]() {
        setProgress(false, total, total);
        emit finished(total);
      });
      return;
    }
    done = qMin(done + *n, total);
    QMetaObject::invokeMethod(this, [=, this]() { setProgress(true, done, total); });
  }
  qInfo() << "[Reindex] paused at" << done << "/" << total;
}
