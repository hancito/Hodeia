#pragma once

#include "zoterorepository.h"
#include "zoterosource.h"
#include "zoterosync.h"

#include <QAbstractListModel>
#include <QSet>

/// The libraries and collections of Zotero as a flat list in tree order
/// (library, then its collections depth first, by name), each row with its
/// depth, for the collections window.
///
/// Each collection is followed or not on its own.  A row shows Checked when
/// it and all its sub-collections are followed, Unchecked when none is, and
/// PartiallyChecked otherwise; toggling a row applies to it and everything
/// below it (a library row: to all its collections).
class ZoteroCollectionModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int followedCount READ followedCount NOTIFY followedChanged)

public:
  enum Role {
    NameRole = Qt::UserRole + 1,
    DepthRole,
    IsLibraryRole,
    CheckStateRole, ///< Qt::CheckState as an int
    PdfCountRole,
    OnDiskCountRole,
    InProjectCountRole,
  };

  explicit ZoteroCollectionModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  /// Rebuild from a snapshot, with the counts of each collection and the
  /// collections the project follows (unknown ones are dropped).
  void reset(const ZoteroSnapshot &snapshot,
             const QHash<QString, ZoteroSync::CollectionCounts> &counts,
             const QList<FollowedCollection> &followed);

  /// Check or uncheck a row and everything below it.
  Q_INVOKABLE void toggle(int row);

  /// The collections checked now.
  QList<FollowedCollection> followed() const;
  int followedCount() const { return int(m_followed.size()); }

signals:
  void followedChanged();

private:
  struct Row {
    bool isLibrary = false;
    int libraryId = 0;
    QString key; ///< collection key; empty for a library
    QString name;
    int depth = 0;
    ZoteroSync::CollectionCounts counts; ///< zero for a library (a PDF may
                                         ///< sit in several collections)
    int end = 0; ///< one past the last row below this one
  };

  Qt::CheckState checkState(int row) const;
  QString id(const Row &r) const { return ZoteroSync::collectionId(r.libraryId, r.key); }

  QList<Row> m_rows;
  QSet<QString> m_followed; ///< collectionId of the checked collections
};
