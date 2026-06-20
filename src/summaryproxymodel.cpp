#include "headers/summaryproxymodel.h"
#include "headers/summary.h" // Needed for the roles
#include <QSortFilterProxyModel>

SummaryProxyModel::SummaryProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent) {
  // Tell Qt to dynamically re-sort/re-filter when data changes
  setDynamicSortFilter(true);
  setSortRole(SummaryModel::TitleRole);
}

QString SummaryProxyModel::filterText() const { return m_filterText; }

void SummaryProxyModel::setFilterText(const QString &text) {
  if (m_filterText == text)
    return;
  beginFilterChange();
  m_filterText = text;
  endFilterChange();
  emit filterTextChanged();
}

int SummaryProxyModel::sortRole() const {
  return QSortFilterProxyModel::sortRole();
}

void SummaryProxyModel::setSortRole(int role) {
  QSortFilterProxyModel::setSortRole(role);
  emit sortRoleChanged();
  sort(0, sortOrder());
}

bool SummaryProxyModel::isSortDescending() const {
  return sortOrder() == Qt::DescendingOrder;
}

void SummaryProxyModel::setSortDescending(bool descending) {
  auto newOrder = descending ? Qt::DescendingOrder : Qt::AscendingOrder;
  if (sortOrder() == newOrder)
    return;
  sort(0, newOrder);
  emit sortDescendingChanged();
}

QHash<int, QByteArray> SummaryProxyModel::roleNames() const {
  return sourceModel()->roleNames();
}

QVariant SummaryProxyModel::get(int row, const QString &roleName) const {
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

bool SummaryProxyModel::filterAcceptsRow(
    int source_row, const QModelIndex &source_parent) const {
  if (m_filterText.isEmpty())
    return true;

  // Get the index in the source model
  QModelIndex idx = sourceModel()->index(source_row, 0, source_parent);

  // CAUTION: Make sure SummaryModel::TitleRole isn't already adding
  // Qt::UserRole! If TitleRole is defined as `Qt::UserRole + 2` in your header,
  // DO NOT add Qt::UserRole again here.
  QString title = sourceModel()->data(idx, SummaryModel::TitleRole).toString();
  QString authors =
      sourceModel()->data(idx, SummaryModel::AuthorsRole).toString();

  QString combined = (title + " " + authors).toLower();
  return combined.contains(m_filterText.toLower());
}

bool SummaryProxyModel::lessThan(const QModelIndex &source_left,
                                 const QModelIndex &source_right) const {
  QVariant leftData = sourceModel()->data(source_left, sortRole());
  QVariant rightData = sourceModel()->data(source_right, sortRole());

  if (leftData.userType() != rightData.userType())
    return leftData.userType() < rightData.userType();
  return QVariant::compare(leftData, rightData) < 0;
}
