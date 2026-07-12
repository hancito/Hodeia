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

  while (true) {
    auto stepResult = stmt.step();
    if (!stepResult || !stepResult.value())
      break;
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

QVariant QueryModel::dataAt(int index, const QString &role) const {
  if (index < 0 || index >= m_rows.size())
    return QVariant();
  auto names = roleNames();
  int roleKey = -1;
  for (auto it = names.begin(); it != names.end(); ++it) {
    if (it.value() == role.toUtf8()) {
      roleKey = it.key();
      break;
    }
  }
  if (roleKey < 0)
    return QVariant();
  QModelIndex idx = createIndex(index, 0);
  return data(idx, roleKey);
}
