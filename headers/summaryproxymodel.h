#pragma once
#include "headers/summary.h"
#include <QAbstractListModel>

class SummaryProxyModel : public QSortFilterProxyModel {
  Q_OBJECT

  Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY
                 filterTextChanged)
  Q_PROPERTY(
      int sortRole READ sortRole WRITE setSortRole NOTIFY sortRoleChanged)
  Q_PROPERTY(bool sortDescending READ isSortDescending WRITE setSortDescending
                 NOTIFY sortDescendingChanged)

public:
  enum SortRoles { SortByTitle = 1, SortByDate, SortByPages, SortByModel };

  explicit SummaryProxyModel(QObject *parent = nullptr);

  void setSourceModel(SummaryModel *source);

  QString filterText() const;
  int sortRole() const;
  bool isSortDescending() const;

  void setFilterText(const QString &text);
  void setSortRole(int role);
  void setSortDescending(bool descending);

  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE QVariant get(int row) const;

protected:
  bool filterAcceptsRow(int sourceRow) const;

private:
  void refresh();

  SummaryModel *m_source = nullptr;
  QString m_filterText;
  int m_sortRole = SortByTitle;
  bool m_sortDescending = false;
  QVector<int> m_filteredRows; // indices into source model

signals:
  void filterTextChanged();
  void sortRoleChanged();
  void sortDescendingChanged();
};
