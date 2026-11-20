#pragma once

#include <QString>
#include <expected>
#include <optional>

class SqliteDb;
class SqliteVecManager;

/// Embedding again the chunks of a project, as recorded by the migration
/// (Maintenance task "reembed").  Batch by batch: each batch is committed
/// with its progress (the last chunk rowid done), so the work resumes where
/// it stopped, after a project switch or a restart.  The text of the chunks
/// is not touched.
namespace Reindexer {

struct Status {
  int done = 0;  ///< chunks embedded again
  int total = 0; ///< chunks of the project
};

/// The work left in `db`; nullopt when there is none.
std::optional<Status> status(SqliteDb &db);

/// Embed the next `batch` chunks again.  Returns how many were done; 0 once
/// every chunk is done (the task is then removed).  The embedding runs
/// outside any transaction: the database is locked only to write.
std::expected<int, QString> step(SqliteDb &db, SqliteVecManager &embeddings,
                                 int batch);

} // namespace Reindexer
