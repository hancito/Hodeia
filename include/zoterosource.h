#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <expected>

/* What HodeIA reads from a Zotero library.  Always read-only: HodeIA never
   writes to Zotero.  Items, collections and attachments are identified by
   their Zotero keys (8 characters, stable across syncs), never by the
   numeric ids of zotero.sqlite. */

/// A library: the user's own ("user") or a group library ("group").
struct ZoteroLibrary {
  int id = 0;     ///< libraryID, only meaningful inside one snapshot
  QString type;   ///< "user" or "group"
  QString name;   ///< group name; empty for the user library
};

struct ZoteroCollection {
  int libraryId = 0;
  QString key;
  QString name;
  QString parentKey; ///< empty for a top-level collection
};

/// Bibliographic data of a Zotero item (the parent of a PDF).
struct ZoteroMeta {
  QString title;
  QStringList authors; ///< "First Last", in Zotero's order (authors only)
  QString year;        ///< four digits, empty when unknown
  QString journal;     ///< publication, book or proceedings title
  QString doi;
  QString itemType;    ///< Zotero type name: journalArticle, book...
  QStringList tags;

  bool operator==(const ZoteroMeta &) const = default;
};

/// A PDF attachment of the library, with the data of its parent item.
struct ZoteroPdf {
  int libraryId = 0;
  QString itemKey;       ///< parent item; the attachment itself if standalone
  QString attachmentKey;
  QString path;          ///< absolute path; empty if it cannot be resolved
  bool onDisk = false;   ///< the file exists (Zotero downloads on demand)
  QString storageHash;   ///< MD5 of the file as Zotero last synced it; may be empty
  QString dateModified;  ///< latest of the item's and the attachment's (UTC)
  QStringList collectionKeys; ///< collections that hold the item directly
  ZoteroMeta meta;
};

/// Everything the connector needs, read in one go.
struct ZoteroSnapshot {
  QList<ZoteroLibrary> libraries;
  QList<ZoteroCollection> collections;
  QList<ZoteroPdf> pdfs; ///< PDFs out of the trash, files on disk or not
};

/// A source of Zotero data.  Today the SQLite database; later the local
/// API of Zotero 7 (http://localhost:23119/api/) can implement it too.
class ZoteroSource {
public:
  virtual ~ZoteroSource() = default;
  /// Read the whole library.  On failure, a message for the user.
  virtual std::expected<ZoteroSnapshot, QString> load() = 0;
};
