#pragma once

#include "zoterorepository.h"
#include "zoterosource.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <functional>

/// What to do with the project after reading Zotero: which PDFs to store,
/// propose, mark imported or removed.  Pure logic, no database and no file
/// access (the hash function is passed in): see ZoteroController for the
/// glue.
namespace ZoteroSync {

/// Identifies a PDF across libraries: "<library id>:<attachment key>".
QString pdfId(int libraryId, const QString &attachmentKey);
inline QString pdfId(const ZoteroPdf &p) { return pdfId(p.libraryId, p.attachmentKey); }
inline QString pdfId(const ZoteroItemRow &r) { return pdfId(r.libraryId, r.attachmentKey); }
/// Identifies a collection across libraries: "<library id>:<key>".
QString collectionId(int libraryId, const QString &key);

/// MD5 of a file, hex, as at import; empty if it cannot be read.
QString fileMd5(const QString &path);

using HashFn = std::function<QString(const QString &path)>;

/// The hash of each PDF of `pdfs` that is on disk, by pdfId: the one the
/// project already has if Zotero has not touched the attachment since, else
/// `md5(path)`.  Not Zotero's storageHash: it is the file as last synced,
/// and a local file that differs would never be found in the project.
QHash<QString, QString> resolveHashes(const QList<ZoteroPdf> &pdfs,
                                      const QList<ZoteroItemRow> &known,
                                      const HashFn &md5 = fileMd5);

/// The PDFs of `snapshot` held by the followed collections, once each.
QList<ZoteroPdf> followedPdfs(const ZoteroSnapshot &snapshot,
                              const QList<FollowedCollection> &followed);

struct Plan {
  /// Rows to upsert: new PDFs (Pending, or Ingested when the project already
  /// has the file) and known ones that changed in Zotero or came back.
  QList<ZoteroItemRow> store;
  QList<int> ingestedIds; ///< known rows whose file is now in the project
  QList<int> pendingIds;  ///< imported rows whose file Zotero replaced
  QList<int> ignoredIds;  ///< imported rows deleted from the project since
  QList<int> removedIds;  ///< known rows no longer in the followed collections
  QList<ZoteroPdf> missing; ///< followed PDFs not on disk (not downloaded)
};

/// Compare the followed PDFs with what the project knows.
/// `hashes`: from resolveHashes(); `indexed`: hashes of the project's papers.
Plan plan(const QList<ZoteroPdf> &followed, const QList<ZoteroItemRow> &known,
          const QHash<QString, QString> &hashes, const QSet<QString> &indexed);

/// The PDFs to propose for import: pending, still in Zotero, not in the
/// project yet.
QList<ZoteroItemRow> proposals(const QList<ZoteroItemRow> &rows,
                               const QSet<QString> &indexed);

/// The PDFs the user ignored that could be proposed again: still in the
/// followed collections, not in the project.
QList<ZoteroItemRow> ignored(const QList<ZoteroItemRow> &rows,
                             const QSet<QString> &indexed);

struct CollectionCounts {
  int pdfs = 0;      ///< PDF attachments held directly
  int onDisk = 0;    ///< of which downloaded
  int inProject = 0; ///< of which already in the project
};

/// Counts for every collection of the snapshot, by collectionId().  No file
/// is read: "in the project" uses the stored hashes and Zotero's storageHash.
QHash<QString, CollectionCounts>
countByCollection(const ZoteroSnapshot &snapshot,
                  const QList<ZoteroItemRow> &known, const QSet<QString> &indexed);

} // namespace ZoteroSync
