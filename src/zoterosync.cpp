#include "zoterosync.h"

#include <QCryptographicHash>
#include <QFile>

namespace ZoteroSync {

QString pdfId(int libraryId, const QString &attachmentKey) {
  return QString::number(libraryId) + ':' + attachmentKey;
}

QString collectionId(int libraryId, const QString &key) {
  return QString::number(libraryId) + ':' + key;
}

QString fileMd5(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  QCryptographicHash hasher(QCryptographicHash::Md5);
  if (!hasher.addData(&file))
    return {};
  return QString::fromLatin1(hasher.result().toHex());
}

QHash<QString, QString> resolveHashes(const QList<ZoteroPdf> &pdfs,
                                      const QList<ZoteroItemRow> &known,
                                      const HashFn &md5) {
  QHash<QString, const ZoteroItemRow *> byId;
  for (const ZoteroItemRow &r : known)
    byId.insert(pdfId(r), &r);

  QHash<QString, QString> hashes;
  for (const ZoteroPdf &p : pdfs) {
    if (!p.onDisk)
      continue;
    const QString id = pdfId(p);
    const ZoteroItemRow *k = byId.value(id);
    QString hash = (k && k->dateModified == p.dateModified) ? k->paperHash : QString();
    if (hash.isEmpty())
      hash = md5(p.path);
    if (!hash.isEmpty())
      hashes.insert(id, hash);
  }
  return hashes;
}

QList<ZoteroPdf> followedPdfs(const ZoteroSnapshot &snapshot,
                              const QList<FollowedCollection> &followed) {
  QSet<QString> followedIds;
  for (const FollowedCollection &c : followed)
    followedIds.insert(collectionId(c.libraryId, c.key));

  QList<ZoteroPdf> out;
  for (const ZoteroPdf &p : snapshot.pdfs)
    for (const QString &key : p.collectionKeys)
      if (followedIds.contains(collectionId(p.libraryId, key))) {
        out.append(p);
        break;
      }
  return out;
}

Plan plan(const QList<ZoteroPdf> &followed, const QList<ZoteroItemRow> &known,
          const QHash<QString, QString> &hashes, const QSet<QString> &indexed) {
  QHash<QString, const ZoteroItemRow *> byId;
  for (const ZoteroItemRow &r : known)
    byId.insert(pdfId(r), &r);

  Plan out;
  QSet<QString> present; // followed PDFs, downloaded or not
  for (const ZoteroPdf &p : followed) {
    const QString id = pdfId(p);
    present.insert(id);
    const QString hash = hashes.value(id);
    if (hash.isEmpty()) { // not on disk, or unreadable
      out.missing.append(p);
      continue;
    }

    ZoteroItemRow row;
    row.libraryId = p.libraryId;
    row.itemKey = p.itemKey;
    row.attachmentKey = p.attachmentKey;
    row.paperHash = hash;
    row.meta = p.meta;
    row.dateModified = p.dateModified;

    const ZoteroItemRow *k = byId.value(id);
    if (!k) {
      // The user may have imported the file by hand before.
      row.status = indexed.contains(hash) ? ZoteroStatus::Ingested
                                          : ZoteroStatus::Pending;
      out.store.append(row);
      continue;
    }
    row.id = k->id;
    row.status = k->status;
    if (k->paperHash != hash || k->meta != p.meta ||
        k->dateModified != p.dateModified || k->itemKey != p.itemKey ||
        k->removedAt.isValid())
      out.store.append(row);

    if (k->status == ZoteroStatus::Pending && indexed.contains(hash))
      out.ingestedIds.append(k->id);
    else if (k->status == ZoteroStatus::Ingested && !indexed.contains(hash)) {
      // Zotero holds a new file: propose it.  Same file missing from the
      // project: the user deleted the paper; not proposed again, but listed
      // with the ignored PDFs, from where it can be proposed again.
      if (k->paperHash != hash)
        out.pendingIds.append(k->id);
      else
        out.ignoredIds.append(k->id);
    }
  }

  for (const ZoteroItemRow &r : known)
    if (!r.removedAt.isValid() && !present.contains(pdfId(r)))
      out.removedIds.append(r.id);
  return out;
}

QList<ZoteroItemRow> proposals(const QList<ZoteroItemRow> &rows,
                               const QSet<QString> &indexed) {
  QList<ZoteroItemRow> out;
  for (const ZoteroItemRow &r : rows)
    if (r.status == ZoteroStatus::Pending && !r.removedAt.isValid() &&
        !indexed.contains(r.paperHash))
      out.append(r);
  return out;
}

QList<ZoteroItemRow> ignored(const QList<ZoteroItemRow> &rows,
                             const QSet<QString> &indexed) {
  QList<ZoteroItemRow> out;
  for (const ZoteroItemRow &r : rows)
    if (r.status == ZoteroStatus::Ignored && !r.removedAt.isValid() &&
        !indexed.contains(r.paperHash))
      out.append(r);
  return out;
}

QHash<QString, CollectionCounts>
countByCollection(const ZoteroSnapshot &snapshot,
                  const QList<ZoteroItemRow> &known, const QSet<QString> &indexed) {
  QHash<QString, const ZoteroItemRow *> byId;
  for (const ZoteroItemRow &r : known)
    byId.insert(pdfId(r), &r);

  QHash<QString, CollectionCounts> counts;
  for (const ZoteroPdf &p : snapshot.pdfs) {
    const ZoteroItemRow *k = byId.value(pdfId(p));
    const bool inProject = (k && indexed.contains(k->paperHash)) ||
                           (!p.storageHash.isEmpty() && indexed.contains(p.storageHash));
    for (const QString &key : p.collectionKeys) {
      CollectionCounts &c = counts[collectionId(p.libraryId, key)];
      ++c.pdfs;
      c.onDisk += p.onDisk;
      c.inProject += inProject;
    }
  }
  return counts;
}

} // namespace ZoteroSync
