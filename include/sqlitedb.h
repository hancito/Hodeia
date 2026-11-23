#pragma once

#include <QDateTime>
#include <QString>
#include <expected>

struct sqlite3;
struct sqlite3_stmt;

class SqliteDb;

class SqliteStatement {
  friend class SqliteDb;

public:
  SqliteStatement();
  ~SqliteStatement();

  // Move-only
  SqliteStatement(SqliteStatement &&other) noexcept;
  SqliteStatement &operator=(SqliteStatement &&other) noexcept;
  SqliteStatement(const SqliteStatement &) = delete;
  SqliteStatement &operator=(const SqliteStatement &) = delete;

  std::expected<bool, QString> bind(int index, const QString &value);
  std::expected<bool, QString> bind(int index, int value);
  std::expected<bool, QString> bind(int index, int64_t value);
  std::expected<bool, QString> bind(int index, double value);
  std::expected<bool, QString> bind(int index, const QDateTime &dt);
  std::expected<bool, QString> bind(int index, const QByteArray &blob);

  /// One step: true for a row, false when done.  Any other SQLite result
  /// (a broken constraint, a locked database, an I/O error) is an error.
  std::expected<bool, QString> step();
  /// Execute a statement that returns no rows (INSERT, UPDATE, DELETE):
  /// true, or an error as for step().
  std::expected<bool, QString> exec();
  void reset();
  void resetAndClearBindings();

  int columnInt(int index) const;
  int64_t columnInt64(int index) const;
  double columnDouble(int index) const;
  QString columnText(int index) const;
  QByteArray columnBlob(int index) const;
  QDateTime columnDateTime(int index) const;
  int columnType(int index) const;
  int columnCount() const;
  bool columnIsNull(int index) const;

  const char *columnTextRaw(int index) const;
  const unsigned char *columnBlobRaw(int index) const;
  int columnBytes(int index) const;

  bool isValid() const { return m_stmt != nullptr; }

  sqlite3_stmt *handle() const { return m_stmt; }

private:
  sqlite3_stmt *m_stmt = nullptr;
  QString m_errorMsg;
};

class SqliteDb {
public:
  SqliteDb();
  ~SqliteDb();

  // Move-only
  SqliteDb(SqliteDb &&other) noexcept;
  SqliteDb &operator=(SqliteDb &&other) noexcept;
  SqliteDb(const SqliteDb &) = delete;
  SqliteDb &operator=(const SqliteDb &) = delete;

  std::expected<void, QString> open(const QString &path);
  void close();
  bool isOpen() const { return m_db != nullptr; }

  // Execute a statement that returns no rows (CREATE, INSERT, DELETE, UPDATE)
  std::expected<void, QString> exec(const QString &sql);
  /// Write the WAL back into the database file and truncate it (after a
  /// large write, so the -wal file does not stay large).
  std::expected<void, QString> checkpoint();

  // Prepare a statement for repeated use
  SqliteStatement prepare(const QString &sql);

  // Transaction support
  std::expected<void, QString> transaction();
  std::expected<void, QString> commit();
  std::expected<void, QString> rollback();

  int64_t lastInsertRowId() const;

  sqlite3 *handle() const { return m_db; }

private:
  sqlite3 *m_db = nullptr;
};
