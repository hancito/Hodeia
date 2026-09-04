#include "queryproxymodel.h"
#include "query.h"

QueryProxyModel::QueryProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent) {
  setDynamicSortFilter(true);
  setSortRole(QueryModel::DateRole);
}

QString QueryProxyModel::filterText() const { return m_filterText; }

void QueryProxyModel::setFilterText(const QString &text) {
  if (m_filterText == text)
    return;
  beginFilterChange();
  m_filterText = text;
  endFilterChange();
  emit filterTextChanged();
}

int QueryProxyModel::sortRole() const {
  return QSortFilterProxyModel::sortRole();
}

void QueryProxyModel::setSortRole(int role) {
  QSortFilterProxyModel::setSortRole(role);
  emit sortRoleChanged();
  sort(0, sortOrder());
}

bool QueryProxyModel::isSortDescending() const {
  return sortOrder() == Qt::DescendingOrder;
}

void QueryProxyModel::setSortDescending(bool descending) {
  auto newOrder = descending ? Qt::DescendingOrder : Qt::AscendingOrder;
  if (sortOrder() == newOrder)
    return;
  sort(0, newOrder);
  emit sortDescendingChanged();
}

QHash<int, QByteArray> QueryProxyModel::roleNames() const {
  return sourceModel()->roleNames();
}

QVariant QueryProxyModel::get(int row, const QString &roleName) const {
  QHash<int, QByteArray> roles = roleNames();
  int roleInt = -1;
  for (auto it = roles.begin(); it != roles.end(); ++it) {
    if (it.value() == roleName.toUtf8()) {
      roleInt = it.key();
      break;
    }
  }
  if (roleInt == -1)
    return QVariant();

  QModelIndex idx = QSortFilterProxyModel::index(row, 0, QModelIndex());
  return data(idx, roleInt);
}

bool QueryProxyModel::filterAcceptsRow(int source_row,
                                       const QModelIndex &source_parent) const {
  if (m_filterText.isEmpty())
    return true;

  QModelIndex idx = sourceModel()->index(source_row, 0, source_parent);

  QString queryText =
      sourceModel()->data(idx, QueryModel::QueryTextRole).toString();
  QString response =
      sourceModel()->data(idx, QueryModel::ResponseRole).toString();

  QString combined = (queryText + " " + response).toLower();
  return combined.contains(m_filterText.toLower());
}

bool QueryProxyModel::lessThan(const QModelIndex &source_left,
                               const QModelIndex &source_right) const {
  QVariant leftData = sourceModel()->data(source_left, sortRole());
  QVariant rightData = sourceModel()->data(source_right, sortRole());

  if (leftData.userType() != rightData.userType())
    return leftData.userType() < rightData.userType();
  return QVariant::compare(leftData, rightData) < 0;
}
