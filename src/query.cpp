#include "headers/query.h"
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

QueryModel::QueryModel(QObject *parent) : QAbstractListModel(parent) {}

int QueryModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  return m_rows.size();
}

QVariant QueryModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
    return {};

  const auto &row = m_rows[index.row()];
  switch (role) {
  case IdRole:
    return row.id;
  case QueryTextRole:
    return row.queryText;
  case ResponseRole:
    return row.response;
  case ModelRole:
    return row.model;
  case DateRole:
    return row.date;
  case FormattedDateRole:
    return formatDate(row.date);
  default:
    return {};
  }
}

QHash<int, QByteArray> QueryModel::roleNames() const {
  return {{IdRole, "id"},
          {QueryTextRole, "queryText"},
          {ResponseRole, "response"},
          {DateRole, "date"},
          {ModelRole, "model"},
          {FormattedDateRole, "formattedDate"}};
}

QString QueryModel::formatDate(const QDateTime &dt) const {
  if (dt.isValid()) {
    return dt.toLocalTime().toString("yyyy-MM-dd hh:mm:ss");
  }
  return {};
}

void QueryModel::loadAll() {
  beginResetModel();
  m_rows.clear();

  QSqlQuery query(
      "SELECT id, query, response, date, model FROM Queries ORDER BY id");

  while (query.next()) {
    QueryRow row;
    row.id = query.value(0).toInt();
    row.queryText = query.value(1).toString();
    row.response = query.value(2).toString();
    row.date = query.value(3).toDateTime();
    row.model = query.value(4).toString();
    m_rows.append(row);
  }

  endResetModel();
}

void QueryModel::insert(const QueryRow &row) {
  beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size());
  m_rows.append(row);
  endInsertRows();

  QSqlQuery q;
  q.prepare("INSERT INTO Queries (query, response, date, model) "
            "VALUES (:query, :response, :date, :model)");
  q.bindValue(":query", row.queryText);
  q.bindValue(":response", row.response);
  q.bindValue(":date", row.date);
  q.bindValue(":model", row.model);

  if (!q.exec()) {
    qDebug() << "Insert Query failed:" << q.lastError().text();
  }
}

void QueryModel::removeRow(int index) {
  if (index < 0 || index >= m_rows.size())
    return;

  int dbId = m_rows[index].id;
  beginRemoveRows(QModelIndex(), index, index);
  m_rows.removeAt(index);
  endRemoveRows();

  QSqlQuery q;
  q.prepare("DELETE FROM Queries WHERE id = :id");
  q.bindValue(":id", dbId);
  if (!q.exec()) {
    qDebug() << "Delete Query failed:" << q.lastError().text();
  }
}

void QueryModel::clear() {
  beginResetModel();
  m_rows.clear();
  endResetModel();

  QSqlQuery q("DELETE FROM Queries");
  if (!q.exec()) {
    qDebug() << "Clear Queries failed:" << q.lastError().text();
  }
}

QueryRow QueryModel::getRow(int index) const {
  if (index < 0 || index >= m_rows.size())
    return QueryRow{};
  return m_rows[index];
}
