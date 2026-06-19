#include "headers/summaryproxymodel.h"
#include <QSortFilterProxyModel>

SummaryProxyModel::SummaryProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent) {}

void SummaryProxyModel::setSourceModel(SummaryModel *source) {
  m_source = source;
  connect(source, &QAbstractListModel::modelReset, this,
          [this]() { refresh(); });
  connect(source, &QAbstractListModel::rowsInserted, this,
          [this]() { refresh(); });
  connect(source, &QAbstractListModel::rowsRemoved, this,
          [this]() { refresh(); });
  refresh();
}

QString SummaryProxyModel::filterText() const { return m_filterText; }

int SummaryProxyModel::sortRole() const { return m_sortRole; }

bool SummaryProxyModel::isSortDescending() const { return m_sortDescending; }

void SummaryProxyModel::setFilterText(const QString &text) {
  if (m_filterText == text)
    return;
  m_filterText = text;
  refresh();
  emit filterTextChanged();
}

void SummaryProxyModel::setSortRole(int role) {
  if (m_sortRole == role)
    return;
  m_sortRole = role;
  refresh();
  emit sortRoleChanged();
}

void SummaryProxyModel::setSortDescending(bool descending) {
  if (m_sortDescending == descending)
    return;
  m_sortDescending = descending;
  refresh();
  emit sortDescendingChanged();
}

int SummaryProxyModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  qDebug() << "SummaryProxyModel::rowCount()" << m_filteredRows.size();
  return m_filteredRows.size();
}

QVariant SummaryProxyModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 ||
      index.row() >= m_filteredRows.size() || !m_source)
    return {};

  int sourceRow = m_filteredRows[index.row()];
  return m_source->data(m_source->index(sourceRow, 0), role);
}

QHash<int, QByteArray> SummaryProxyModel::roleNames() const {
  if (m_source)
    return m_source->roleNames();
  return QAbstractListModel::roleNames();
}

QVariant SummaryProxyModel::get(int row) const {
  if (!m_source || row < 0 || row >= m_filteredRows.size())
    return QVariant();

  int sourceRow = m_filteredRows[row];
  SummaryRow r = m_source->getRow(sourceRow);

  QVariantMap map;
  map["id"] = r.id;
  map["paperHash"] = r.paperHash;
  map["title"] = r.title;
  map["authors"] = r.authors;
  map["pagesNum"] = r.pagesNum;
  map["journal"] = r.journal;
  map["year"] = r.year;
  map["topics"] = r.topics;
  map["llmModel"] = r.model;
  map["summaryJson"] = r.summaryJson;
  map["date"] = r.date.toString(Qt::ISODate);
  map["formattedDate"] = m_source->formatDate(r.date);
  return map;
}

bool SummaryProxyModel::filterAcceptsRow(int sourceRow) const {
  if (!m_source || m_filterText.isEmpty())
    return true;

  auto idx = m_source->index(sourceRow, 0);
  if (!idx.isValid())
    return false;

  QString title =
      m_source->data(idx, Qt::UserRole + SummaryModel::TitleRole).toString();
  QString authors =
      m_source->data(idx, Qt::UserRole + SummaryModel::AuthorsRole).toString();
  QString journal =
      m_source->data(idx, Qt::UserRole + SummaryModel::JournalRole).toString();
  QString year =
      m_source->data(idx, Qt::UserRole + SummaryModel::YearRole).toString();
  QString topics =
      m_source->data(idx, Qt::UserRole + SummaryModel::TopicsRole).toString();
  QString model =
      m_source->data(idx, Qt::UserRole + SummaryModel::ModelRole).toString();

  QString combined = (title + " " + authors + " " + journal + " " + year + " " +
                      topics + " " + model)
                         .toLower();

  return combined.contains(m_filterText.toLower());
}

void SummaryProxyModel::refresh() {
  if (!m_source)
    return;

  beginResetModel();

  // Collect source row indices that pass the filter
  QVector<int> candidates;
  for (int i = 0; i < m_source->rowCount(); ++i) {
    if (filterAcceptsRow(i))
      candidates.append(i);
  }

  // Sort candidates
  std::sort(candidates.begin(), candidates.end(), [this](int a, int b) {
    auto idxA = m_source->index(a, 0);
    auto idxB = m_source->index(b, 0);

    QVariant va, vb;
    switch (m_sortRole) {
    case SortByDate:
      va = m_source->data(idxA, Qt::UserRole + SummaryModel::DateRole);
      vb = m_source->data(idxB, Qt::UserRole + SummaryModel::DateRole);
      break;
    case SortByPages:
      va = m_source->data(idxA, Qt::UserRole + SummaryModel::PagesNumRole);
      vb = m_source->data(idxB, Qt::UserRole + SummaryModel::PagesNumRole);
      break;
    case SortByModel:
      va = m_source->data(idxA, Qt::UserRole + SummaryModel::ModelRole);
      vb = m_source->data(idxB, Qt::UserRole + SummaryModel::ModelRole);
      break;
    default:
      va = m_source->data(idxA, Qt::UserRole + SummaryModel::TitleRole);
      vb = m_source->data(idxB, Qt::UserRole + SummaryModel::TitleRole);
      break;
    }

    if (m_sortDescending)
      return QVariant::compare(va, vb) > 0;
    return QVariant::compare(va, vb) < 0;
  });

  m_filteredRows = candidates;
  endResetModel();
}
