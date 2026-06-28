#pragma once

#include "headers/summary.h"
#include <QObject>
#include <QString>

class DatabaseManager;

/// Helper that retrieves paper context from the database.
/// Lives in the main thread (reads from DatabaseManager which is not
/// thread-safe).
class BatchWorker : public QObject {
  Q_OBJECT

public:
  explicit BatchWorker(DatabaseManager *database, QObject *parent = nullptr);

  /// Retrieve all chunks for a paper hash and concatenate them into a
  /// single context string, ordered by page number.
  Q_INVOKABLE QString retrieveContext(const QString &paperHash) const;

signals:

private:
  DatabaseManager *m_database = nullptr;
};
