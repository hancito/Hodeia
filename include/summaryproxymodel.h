#pragma once
#include <QSortFilterProxyModel>

class SummaryModel;

class SummaryProxyModel : public QSortFilterProxyModel {
  Q_OBJECT
  Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY
                 filterTextChanged)
  Q_PROPERTY(
      int sortRole READ sortRole WRITE setSortRole NOTIFY sortRoleChanged)
  Q_PROPERTY(bool sortDescending READ isSortDescending WRITE setSortDescending
                 NOTIFY sortDescendingChanged)

public:
  explicit SummaryProxyModel(QObject *parent = nullptr);

  QString filterText() const;
  void setFilterText(const QString &text);

  int sortRole() const;
  void setSortRole(int role);

  bool isSortDescending() const;
  void setSortDescending(bool descending);

  Q_INVOKABLE QVariant get(int index, const QString &roleName) const;

  QHash<int, QByteArray> roleNames() const override;

protected:
  // Qt calls this to decide if a row should be visible
  bool filterAcceptsRow(int source_row,
                        const QModelIndex &source_parent) const override;

  // Qt calls this to sort columns
  bool lessThan(const QModelIndex &source_left,
                const QModelIndex &source_right) const override;

signals:
  void filterTextChanged();
  void sortRoleChanged();
  void sortDescendingChanged();

private:
  QString m_filterText;
};
