#include "headers/query.h"

QueryModel::QueryModel(QObject *parent) : QAbstractListModel(parent) {}

void QueryModel::setDatabase(SqliteDb &db) { m_db = &db; }

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
  case LlmModelRole:
    return row.llmodel;
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
          {LlmModelRole, "model"},
          {FormattedDateRole, "formattedDate"}};
}

QString QueryModel::formatDate(const QDateTime &dt) const {
  if (dt.isValid()) {
    return dt.toLocalTime().toString("yyyy-MM-dd hh:mm:ss");
  }
  return {};
}

void QueryModel::loadAll() {
  if (!m_db)
    return;

  beginResetModel();
  m_rows.clear();

  SqliteStatement stmt =
      m_db->prepare("SELECT id, query, response, date, llmodel FROM Queries "
                    "ORDER BY id");

  while (stmt.step()) {
    QueryRow row;
    row.id = stmt.columnInt(0);
    row.queryText = stmt.columnText(1);
    row.response = stmt.columnText(2);
    row.date = stmt.columnDateTime(3);
    row.llmodel = stmt.columnText(4);
    m_rows.append(row);
  }

  endResetModel();
}

void QueryModel::insert(const QueryRow &row) {
  if (!m_db)
    return;

  beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size());
  m_rows.append(row);
  endInsertRows();

  SqliteStatement stmt =
      m_db->prepare("INSERT INTO Queries (query, response, date, llmodel) "
                    "VALUES (?, ?, ?, ?)");
  stmt.bind(1, row.queryText);
  stmt.bind(2, row.response);
  stmt.bind(3, row.date);
  stmt.bind(4, row.llmodel);

  if (!stmt.exec()) {
    qDebug() << "Insert Query failed:" << stmt.errorMsg();
  }
}

void QueryModel::removeRow(int index) {
  if (!m_db || index < 0 || index >= m_rows.size())
    return;

  int dbId = m_rows[index].id;
  beginRemoveRows(QModelIndex(), index, index);
  m_rows.removeAt(index);
  endRemoveRows();

  SqliteStatement stmt = m_db->prepare("DELETE FROM Queries WHERE id = ?");
  stmt.bind(1, dbId);
  if (!stmt.exec()) {
    qDebug() << "Delete Query failed:" << stmt.errorMsg();
  }
}

void QueryModel::clear() {
  if (!m_db)
    return;

  beginResetModel();
  m_rows.clear();
  endResetModel();

  m_db->exec("DELETE FROM Queries");
}

QueryRow QueryModel::getRow(int index) const {
  if (index < 0 || index >= m_rows.size())
    return QueryRow{};
  return m_rows[index];
}
