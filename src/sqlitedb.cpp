#include "sqlitedb.h"
#include <QCoreApplication>
#include <QLibraryInfo>
extern "C" {
#include <Sqlite3/sqlite3.h>
}

/* ── SqliteStatement ─────────────────────────────────────────── */

SqliteStatement::SqliteStatement() = default;

SqliteStatement::~SqliteStatement() {
  if (m_stmt)
    sqlite3_finalize(m_stmt);
}

SqliteStatement::SqliteStatement(SqliteStatement &&other) noexcept
    : m_stmt(other.m_stmt), m_errorMsg(std::move(other.m_errorMsg)) {
  other.m_stmt = nullptr;
}

SqliteStatement &SqliteStatement::operator=(SqliteStatement &&other) noexcept {
  if (this != &other) {
    if (m_stmt)
      sqlite3_finalize(m_stmt);
    m_stmt = other.m_stmt;
    m_errorMsg = std::move(other.m_errorMsg);
    other.m_stmt = nullptr;
  }
  return *this;
}

std::expected<bool, QString> SqliteStatement::bind(int index,
                                                   const QString &value) {
  if (!m_stmt)
    return std::unexpected{m_errorMsg};
  QByteArray utf8 = value.toUtf8();
  int rc = sqlite3_bind_text(m_stmt, index, utf8.constData(), utf8.size(),
                             SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) {
    return std::unexpected{
        QString::fromUtf8(sqlite3_errmsg(sqlite3_db_handle(m_stmt)))};
  }
  return true;
}

std::expected<bool, QString> SqliteStatement::bind(int index, int value) {
  if (!m_stmt)
    return std::unexpected{m_errorMsg};
  int rc = sqlite3_bind_int(m_stmt, index, value);
  if (rc != SQLITE_OK) {
    return std::unexpected{
        QString::fromUtf8(sqlite3_errmsg(sqlite3_db_handle(m_stmt)))};
  }
  return true;
}

std::expected<bool, QString> SqliteStatement::bind(int index, int64_t value) {
  if (!m_stmt)
    return std::unexpected{m_errorMsg};
  int rc = sqlite3_bind_int64(m_stmt, index, value);
  if (rc != SQLITE_OK) {
    return std::unexpected{
        QString::fromUtf8(sqlite3_errmsg(sqlite3_db_handle(m_stmt)))};
  }
  return true;
}

std::expected<bool, QString> SqliteStatement::bind(int index, double value) {
  if (!m_stmt)
    return std::unexpected{m_errorMsg};
  int rc = sqlite3_bind_double(m_stmt, index, value);
  if (rc != SQLITE_OK) {
    return std::unexpected{
        QString::fromUtf8(sqlite3_errmsg(sqlite3_db_handle(m_stmt)))};
  }
  return true;
}

std::expected<bool, QString> SqliteStatement::bind(int index,
                                                   const QDateTime &dt) {
  return bind(index, dt.toString(Qt::ISODate));
}

std::expected<bool, QString> SqliteStatement::bind(int index,
                                                   const QByteArray &blob) {
  if (!m_stmt)
    return std::unexpected{m_errorMsg};
  int rc = sqlite3_bind_blob(m_stmt, index, blob.constData(), blob.size(),
                             SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) {
    return std::unexpected{
        QString::fromUtf8(sqlite3_errmsg(sqlite3_db_handle(m_stmt)))};
  }
  return true;
}

// Anything but SQLITE_ROW or SQLITE_DONE is a failure: not only
// SQLITE_ERROR, but also a broken constraint (SQLITE_CONSTRAINT_*), a locked
// database (SQLITE_BUSY), a full disk, an I/O error...
std::expected<bool, QString> SqliteStatement::step() {
  if (!m_stmt)
    return std::unexpected{m_errorMsg};
  const int rc = sqlite3_step(m_stmt);
  if (rc == SQLITE_ROW)
    return true;
  if (rc == SQLITE_DONE)
    return false;
  m_errorMsg = QString::fromUtf8(sqlite3_errmsg(sqlite3_db_handle(m_stmt)));
  return std::unexpected{m_errorMsg};
}

std::expected<bool, QString> SqliteStatement::exec() {
  auto r = step();
  if (!r)
    return r;
  return true; // ran to completion, or returned a row that nobody reads
}

void SqliteStatement::reset() {
  if (m_stmt)
    sqlite3_reset(m_stmt);
}

void SqliteStatement::resetAndClearBindings() {
  if (!m_stmt)
    return;
  sqlite3_reset(m_stmt);
  sqlite3_clear_bindings(m_stmt);
}

int SqliteStatement::columnInt(int index) const {
  return sqlite3_column_int(m_stmt, index);
}

int64_t SqliteStatement::columnInt64(int index) const {
  return sqlite3_column_int64(m_stmt, index);
}

double SqliteStatement::columnDouble(int index) const {
  return sqlite3_column_double(m_stmt, index);
}

QString SqliteStatement::columnText(int index) const {
  const unsigned char *text = sqlite3_column_text(m_stmt, index);
  return text ? QString::fromUtf8(reinterpret_cast<const char *>(text))
              : QString();
}

QByteArray SqliteStatement::columnBlob(int index) const {
  const void *blob = sqlite3_column_blob(m_stmt, index);
  int bytes = sqlite3_column_bytes(m_stmt, index);
  return blob ? QByteArray(static_cast<const char *>(blob), bytes)
              : QByteArray();
}

QDateTime SqliteStatement::columnDateTime(int index) const {
  const unsigned char *text = sqlite3_column_text(m_stmt, index);
  if (!text)
    return QDateTime();
  return QDateTime::fromString(
      QString::fromUtf8(reinterpret_cast<const char *>(text)), Qt::ISODate);
}

bool SqliteStatement::columnIsNull(int index) const {
  return sqlite3_column_type(m_stmt, index) == SQLITE_NULL;
}

/* ── SqliteDb ────────────────────────────────────────────────── */

SqliteDb::SqliteDb() = default;

SqliteDb::~SqliteDb() { close(); }

SqliteDb::SqliteDb(SqliteDb &&other) noexcept : m_db(other.m_db) {
  other.m_db = nullptr;
}

SqliteDb &SqliteDb::operator=(SqliteDb &&other) noexcept {
  if (this != &other) {
    close();
    m_db = other.m_db;
    other.m_db = nullptr;
  }
  return *this;
}

std::expected<void, QString> SqliteDb::open(const QString &path) {
  if (m_db)
    close();

  // FULLMUTEX: serialize all access to this connection. The connection is
  // shared across the main, ingestion, summarizer and OpenAlex threads, so
  // without fullmutex interleaved prepare/step/finalize is undefined
  // behaviour (SQLITE_MISUSE, torn statements, silent write loss).
  int rc = sqlite3_open_v2(path.toUtf8().constData(), &m_db,
                           SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                               SQLITE_OPEN_FULLMUTEX,
                           nullptr);
  if (rc != SQLITE_OK) {
    QString err = QString::fromUtf8(sqlite3_errmsg(m_db));
    sqlite3_close_v2(m_db);
    m_db = nullptr;
    return std::unexpected{err};
  }

  // Enable WAL mode for better concurrent read/write
  sqlite3_exec(m_db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);

  // Without a busy timeout any cross-connection contention (e.g. a second
  // app instance) fails instantly with SQLITE_BUSY; a 5s window lets writers
  // queue briefly instead of dropping writes.
  sqlite3_exec(m_db, "PRAGMA busy_timeout=5000", nullptr, nullptr, nullptr);

  // Enable loading of extensions
  sqlite3_enable_load_extension(m_db, 1);

  // Load the sqlite-vec extension from the application directory.  The file
  // name is platform-specific: vec0.so on Linux/macOS, vec0.dll on Windows.
  const QString vec0Name =
#ifdef Q_OS_WIN
      "vec0.dll";
#else
      "vec0.so";
#endif
  QString vec0Path = QCoreApplication::applicationDirPath() + "/" + vec0Name;
  char *load_err = nullptr;
  rc = sqlite3_load_extension(m_db, vec0Path.toUtf8().constData(), nullptr,
                              &load_err);
  if (rc != SQLITE_OK) {
    QString err = load_err ? QString::fromUtf8(load_err)
                           : QString("sqlite3_load_extension failed");
    if (load_err)
      sqlite3_free(load_err);
    close();
    return std::unexpected{err};
  }
  if (load_err)
    sqlite3_free(load_err);

  // Disable extension loading after init
  sqlite3_enable_load_extension(m_db, 0);

  return {};
}

void SqliteDb::close() {
  if (m_db) {
    sqlite3_close_v2(m_db);
    m_db = nullptr;
  }
}

std::expected<void, QString> SqliteDb::exec(const QString &sql) {
  if (!m_db)
    return std::unexpected{"database not open"};

  char *errmsg = nullptr;
  int rc =
      sqlite3_exec(m_db, sql.toUtf8().constData(), nullptr, nullptr, &errmsg);
  if (rc != SQLITE_OK) {
    QString err = QString::fromUtf8(errmsg);
    sqlite3_free(errmsg);
    return std::unexpected{err};
  }
  return {};
}

SqliteStatement SqliteDb::prepare(const QString &sql) {
  SqliteStatement stmt;
  if (!m_db)
    return stmt;

  QByteArray sqlUtf8 = sql.toUtf8();
  int rc = sqlite3_prepare_v2(m_db, sqlUtf8.constData(), sqlUtf8.size(),
                              &stmt.m_stmt, nullptr);
  if (rc != SQLITE_OK) {
    stmt.m_errorMsg = QString::fromUtf8(sqlite3_errmsg(m_db));
    stmt.m_stmt = nullptr;
  }
  return stmt;
}

std::expected<void, QString> SqliteDb::transaction() {
  return exec("BEGIN TRANSACTION");
}
std::expected<void, QString> SqliteDb::commit() { return exec("COMMIT"); }
std::expected<void, QString> SqliteDb::rollback() { return exec("ROLLBACK"); }
int64_t SqliteDb::dataVersion() {
  SqliteStatement stmt = prepare("PRAGMA data_version");
  auto step = stmt.step();
  return step && *step ? stmt.columnInt64(0) : -1;
}

std::expected<void, QString> SqliteDb::checkpoint() {
  return exec("PRAGMA wal_checkpoint(TRUNCATE)");
}

int64_t SqliteDb::lastInsertRowId() const {
  return m_db ? sqlite3_last_insert_rowid(m_db) : 0;
}
