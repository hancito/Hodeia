#pragma once

#include "libraryrepository.h"
#include "sqlitedb.h"
#include <QAbstractListModel>
#include <QDateTime>

class QueryModel : public QAbstractListModel {
  Q_OBJECT

public:
  enum Roles {
    IdRole = Qt::UserRole + 1,
    QueryTextRole,
    ResponseRole,
    DateRole,
    LlmModelRole,
    FormattedDateRole
  };

  explicit QueryModel(QObject *parent = nullptr);
  void setDatabase(SqliteDb &db);

  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE void loadAll();
  Q_INVOKABLE QVariant dataAt(int index, const QString &role) const;

private:
  SqliteDb *m_db = nullptr;
  QVector<QueryRow> m_rows;
  QString formatDate(const QDateTime &dt) const;
};
