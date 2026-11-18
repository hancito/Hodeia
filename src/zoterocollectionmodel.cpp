#include "zoterocollectionmodel.h"

#include <QCoreApplication>
#include <algorithm>
#include <functional>

ZoteroCollectionModel::ZoteroCollectionModel(QObject *parent)
    : QAbstractListModel(parent) {}

int ZoteroCollectionModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant ZoteroCollectionModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() >= m_rows.size())
    return {};
  const Row &r = m_rows[index.row()];
  switch (role) {
  case NameRole:
    return r.name;
  case DepthRole:
    return r.depth;
  case IsLibraryRole:
    return r.isLibrary;
  case CheckStateRole:
    return int(checkState(index.row()));
  case PdfCountRole:
    return r.counts.pdfs;
  case OnDiskCountRole:
    return r.counts.onDisk;
  case InProjectCountRole:
    return r.counts.inProject;
  }
  return {};
}

QHash<int, QByteArray> ZoteroCollectionModel::roleNames() const {
  return {{NameRole, "name"},
          {DepthRole, "depth"},
          {IsLibraryRole, "isLibrary"},
          {CheckStateRole, "checkState"},
          {PdfCountRole, "pdfCount"},
          {OnDiskCountRole, "onDiskCount"},
          {InProjectCountRole, "inProjectCount"}};
}

void ZoteroCollectionModel::reset(
    const ZoteroSnapshot &snapshot,
    const QHash<QString, ZoteroSync::CollectionCounts> &counts,
    const QList<FollowedCollection> &followed) {
  beginResetModel();
  m_rows.clear();
  m_followed.clear();

  QSet<QString> known;
  for (const ZoteroLibrary &lib : snapshot.libraries) {
    const int libRow = int(m_rows.size());
    Row libraryRow;
    libraryRow.isLibrary = true;
    libraryRow.libraryId = lib.id;
    libraryRow.name = lib.type == "user"
                          ? QCoreApplication::translate("ZoteroCollectionModel",
                                                        "My Library")
                          : lib.name;
    m_rows.append(libraryRow);

    // Children by parent key, by name.
    QHash<QString, QList<const ZoteroCollection *>> children;
    for (const ZoteroCollection &c : snapshot.collections)
      if (c.libraryId == lib.id)
        children[c.parentKey].append(&c);
    for (auto &list : children)
      std::sort(list.begin(), list.end(), [](auto *a, auto *b) {
        return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
      });

    // Depth first; a collection whose parent is unknown (in the trash) is
    // not reachable and left out.
    std::function<void(const QString &, int)> addChildren =
        [&](const QString &parentKey, int depth) {
          for (const ZoteroCollection *c : children.value(parentKey)) {
            const int row = int(m_rows.size());
            Row r;
            r.libraryId = lib.id;
            r.key = c->key;
            r.name = c->name;
            r.depth = depth;
            r.counts = counts.value(ZoteroSync::collectionId(lib.id, c->key));
            m_rows.append(r);
            known.insert(id(r));
            addChildren(c->key, depth + 1);
            m_rows[row].end = int(m_rows.size());
          }
        };
    addChildren(QString(), 1);
    m_rows[libRow].end = int(m_rows.size());
  }

  for (const FollowedCollection &f : followed) {
    const QString fid = ZoteroSync::collectionId(f.libraryId, f.key);
    if (known.contains(fid))
      m_followed.insert(fid);
  }
  endResetModel();
  emit followedChanged();
}

Qt::CheckState ZoteroCollectionModel::checkState(int row) const {
  const Row &r = m_rows[row];
  int checked = 0, total = 0;
  for (int i = r.isLibrary ? row + 1 : row; i < r.end; ++i) {
    ++total;
    checked += m_followed.contains(id(m_rows[i]));
  }
  if (total == 0 || checked == 0)
    return Qt::Unchecked;
  return checked == total ? Qt::Checked : Qt::PartiallyChecked;
}

void ZoteroCollectionModel::toggle(int row) {
  if (row < 0 || row >= m_rows.size())
    return;
  const Row &r = m_rows[row];
  const bool check = checkState(row) != Qt::Checked;
  for (int i = r.isLibrary ? row + 1 : row; i < r.end; ++i) {
    if (check)
      m_followed.insert(id(m_rows[i]));
    else
      m_followed.remove(id(m_rows[i]));
  }
  // Ancestors change too: refresh every check box.
  emit dataChanged(index(0), index(int(m_rows.size()) - 1), {CheckStateRole});
  emit followedChanged();
}

QList<FollowedCollection> ZoteroCollectionModel::followed() const {
  QList<FollowedCollection> out;
  for (const Row &r : m_rows)
    if (!r.isLibrary && m_followed.contains(id(r)))
      out.append({r.libraryId, r.key, r.name});
  return out;
}
