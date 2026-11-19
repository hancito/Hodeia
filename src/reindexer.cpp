#include "reindexer.h"

#include "sqlitedb.h"
#include "sqlitevec.h"

#include <QList>
#include <QStringList>
#include <Sqlite3/sqlite3.h>

namespace {

constexpr const char *kTask = "reembed";

QString lastError(SqliteDb &db) {
  return QString::fromUtf8(sqlite3_errmsg(db.handle()));
}

/// The progress of the task (last rowid done), nullopt if there is no task.
std::optional<qint64> progress(SqliteDb &db) {
  SqliteStatement stmt =
      db.prepare("SELECT progress FROM Maintenance WHERE task = ?");
  if (!stmt.isValid())
    return std::nullopt;
  (void)stmt.bind(1, QString(kTask));
  auto step = stmt.step();
  if (!step || !*step)
    return std::nullopt;
  return stmt.columnInt64(0);
}

int count(SqliteDb &db, const QString &sql, qint64 bound) {
  SqliteStatement stmt = db.prepare(sql);
  if (!stmt.isValid())
    return 0;
  if (sql.contains('?'))
    (void)stmt.bind(1, int64_t(bound));
  auto step = stmt.step();
  return step && *step ? stmt.columnInt(0) : 0;
}

} // namespace

namespace Reindexer {

std::optional<Status> status(SqliteDb &db) {
  const auto done = progress(db);
  if (!done)
    return std::nullopt;
  return Status{count(db, "SELECT COUNT(*) FROM chunks WHERE rowid <= ?", *done),
                count(db, "SELECT COUNT(*) FROM chunks", 0)};
}

std::expected<int, QString> step(SqliteDb &db, SqliteVecManager &embeddings,
                                 int batch) {
  const auto after = progress(db);
  if (!after)
    return 0;

  QList<qint64> rowids;
  QStringList texts;
  {
    SqliteStatement stmt = db.prepare("SELECT rowid, chunk_text FROM chunks "
                                      "WHERE rowid > ? ORDER BY rowid LIMIT ?");
    if (!stmt.isValid())
      return std::unexpected(lastError(db));
    (void)stmt.bind(1, int64_t(*after));
    (void)stmt.bind(2, batch);
    while (true) {
      auto step = stmt.step();
      if (!step)
        return std::unexpected(step.error());
      if (!*step)
        break;
      rowids.append(stmt.columnInt64(0));
      texts.append(stmt.columnText(1));
    }
  }

  if (rowids.isEmpty()) {
    SqliteStatement done = db.prepare("DELETE FROM Maintenance WHERE task = ?");
    if (!done.isValid())
      return std::unexpected(lastError(db));
    (void)done.bind(1, QString(kTask));
    if (auto r = done.exec(); !r)
      return std::unexpected(r.error());
    return 0;
  }

  const QVector<float> vectors =
      embeddings.embed(texts, SqliteVecManager::EmbedMode::Passage);
  const int dim = embeddings.embeddingDim();
  if (vectors.isEmpty() || dim <= 0 || vectors.size() != rowids.size() * dim)
    return std::unexpected(
        embeddings.lastError().isEmpty()
            ? QStringLiteral("the embedding model failed")
            : embeddings.lastError());

  if (auto r = db.transaction(); !r)
    return std::unexpected(r.error());
  const auto fail = [&](const QString &error) {
    (void)db.rollback();
    return std::unexpected(error);
  };

  SqliteStatement update =
      db.prepare("UPDATE chunks SET embedding = vec_f32(?) WHERE rowid = ?");
  if (!update.isValid())
    return fail(lastError(db));
  for (qsizetype i = 0; i < rowids.size(); ++i) {
    const QByteArray blob(reinterpret_cast<const char *>(vectors.constData() + i * dim),
                          dim * sizeof(float));
    update.resetAndClearBindings();
    auto r = update.bind(1, blob);
    if (r)
      r = update.bind(2, int64_t(rowids[i]));
    if (r)
      r = update.exec();
    if (!r)
      return fail(r.error());
  }

  SqliteStatement mark =
      db.prepare("UPDATE Maintenance SET progress = ? WHERE task = ?");
  if (!mark.isValid())
    return fail(lastError(db));
  (void)mark.bind(1, int64_t(rowids.constLast()));
  (void)mark.bind(2, QString(kTask));
  if (auto r = mark.exec(); !r)
    return fail(r.error());
  if (auto r = db.commit(); !r)
    return fail(r.error());
  return int(rowids.size());
}

} // namespace Reindexer
