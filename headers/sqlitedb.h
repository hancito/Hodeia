#pragma once

#include <QDateTime>
#include <QString>

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

  bool bind(int index, const QString &value);
  bool bind(int index, int value);
  bool bind(int index, int64_t value);
  bool bind(int index, double value);
  bool bind(int index, const QDateTime &dt);
  bool bind(int index, const QByteArray &blob);

  bool step();
  // Execute a statement that returns no rows (INSERT, UPDATE, DELETE)
  bool exec();
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
  QString errorMsg() const { return m_errorMsg; }

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

  bool open(const QString &path);
  void close();
  bool isOpen() const { return m_db != nullptr; }

  // Execute a statement that returns no rows (CREATE, INSERT, DELETE, UPDATE)
  bool exec(const QString &sql);

  // Prepare a statement for repeated use
  SqliteStatement prepare(const QString &sql);

  // Transaction support
  bool transaction();
  bool commit();
  bool rollback();

  QString lastError() const { return m_lastError; }
  int64_t lastInsertRowId() const;

  sqlite3 *handle() const { return m_db; }

private:
  sqlite3 *m_db = nullptr;
  QString m_lastError;
};
