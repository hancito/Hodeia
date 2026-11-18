#include "zoterosqlitesource.h"

#include "sqlitedb.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <Sqlite3/sqlite3.h>

namespace {

// itemAttachments.linkMode
constexpr int kImportedFile = 0;
constexpr int kImportedUrl = 1; // a PDF saved from a web page: also in storage/
constexpr int kLinkedFile = 2;

using Error = std::unexpected<QString>;

QString sqlError(SqliteDb &db, const QString &what) {
  return QString("%1: %2").arg(what, QString::fromUtf8(sqlite3_errmsg(db.handle())));
}

/// Run `sql` and call `row(stmt)` for each result row.
template <typename F>
std::expected<void, QString> forEachRow(SqliteDb &db, const QString &sql,
                                        const QString &what, F row) {
  SqliteStatement stmt = db.prepare(sql);
  if (!stmt.isValid())
    return Error(sqlError(db, what));
  while (true) {
    auto step = stmt.step();
    if (!step)
      return Error(what + ": " + step.error());
    if (!*step)
      return {};
    row(stmt);
  }
}

/// Copy zotero.sqlite (and its WAL) into `dir` and open the copy.  The copy
/// may catch Zotero in the middle of a write: it is checked, and retried.
std::expected<void, QString> openCopy(const QString &dataDir,
                                      const QString &dir, SqliteDb &db) {
  const QString src = dataDir + "/zotero.sqlite";
  const QString dst = dir + "/zotero.sqlite";
  QString lastError;
  for (int attempt = 0; attempt < 3; ++attempt) {
    db.close();
    for (const QString suffix : {"", "-wal", "-shm"})
      QFile::remove(dst + suffix);
    if (!QFile::copy(src, dst))
      return Error(QString("Cannot copy %1").arg(QDir::toNativeSeparators(src)));
    if (QFileInfo::exists(src + "-wal") && !QFile::copy(src + "-wal", dst + "-wal"))
      return Error(QString("Cannot copy %1-wal").arg(QDir::toNativeSeparators(src)));
    // The copy is ours: write access only lets SQLite rebuild the WAL index.
    QFile::setPermissions(dst, QFile::ReadOwner | QFile::WriteOwner);
    if (auto r = db.open(dst); !r) {
      lastError = r.error();
      continue;
    }
    SqliteStatement check = db.prepare("PRAGMA quick_check");
    auto step = check.isValid() ? check.step() : std::expected<bool, QString>(false);
    if (step && *step && check.columnText(0) == "ok")
      return {};
    lastError = "the copy of the database is inconsistent";
  }
  db.close();
  return Error("Cannot read the Zotero database (" + lastError + ")");
}

/// `userdata` schema version, -1 if the database is not a Zotero one.
int userdataVersion(SqliteDb &db) {
  SqliteStatement stmt =
      db.prepare("SELECT version FROM version WHERE schema = 'userdata'");
  if (!stmt.isValid())
    return -1;
  auto step = stmt.step();
  return step && *step ? stmt.columnInt(0) : -1;
}

/// Year of a Zotero date ("2014-09-01 2014-09-01", "0000-00-00 Spring 2014").
QString yearOf(const QString &date) {
  static const QRegularExpression fourDigits("\\b(\\d{4})\\b");
  for (auto it = fourDigits.globalMatch(date); it.hasNext();) {
    const QString y = it.next().captured(1);
    if (y != "0000")
      return y;
  }
  return {};
}

struct Creator {
  QString name;
  bool isAuthor = false;
};

struct PdfRow {
  int metaItemId = 0; ///< parent item, or the attachment if standalone
  int linkMode = 0;
  QString rawPath;
  ZoteroPdf pdf;
};

} // namespace

ZoteroSqliteSource::ZoteroSqliteSource(QString dataDir, QString baseAttachmentPath)
    : m_dataDir(QDir::cleanPath(std::move(dataDir))),
      m_baseAttachmentPath(QDir::cleanPath(std::move(baseAttachmentPath))) {
  if (m_baseAttachmentPath == ".")
    m_baseAttachmentPath.clear();
}

std::expected<ZoteroSnapshot, QString> ZoteroSqliteSource::load() {
  if (!QFileInfo(m_dataDir + "/zotero.sqlite").isFile())
    return Error(QString("No zotero.sqlite in %1")
                     .arg(QDir::toNativeSeparators(m_dataDir)));

  QTemporaryDir tmp;
  if (!tmp.isValid())
    return Error("Cannot create a temporary folder: " + tmp.errorString());
  SqliteDb db;
  if (auto r = openCopy(m_dataDir, tmp.path(), db); !r)
    return Error(r.error());

  const int version = userdataVersion(db);
  if (version < 0)
    return Error("zotero.sqlite is not a Zotero database");
  if (version > kTestedUserdataVersion)
    qWarning() << "[Zotero] database schema" << version << "is newer than the"
               << kTestedUserdataVersion << "tested";
  // Name the version in any later error: a schema change is the likely cause.
  const auto fail = [version](const QString &e) {
    return Error(QString("%1 (Zotero schema %2)").arg(e).arg(version));
  };

  ZoteroSnapshot snap;

  // Feeds (RSS) are libraries too: only the user library and groups count.
  if (auto r = forEachRow(
          db,
          "SELECT l.libraryID, l.type, g.name FROM libraries l "
          "LEFT JOIN groups g ON g.libraryID = l.libraryID "
          "WHERE l.type IN ('user', 'group') ORDER BY l.libraryID",
          "libraries",
          [&](SqliteStatement &s) {
            snap.libraries.append({s.columnInt(0), s.columnText(1), s.columnText(2)});
          });
      !r)
    return fail(r.error());

  const QString liveCollections =
      "SELECT collectionID FROM collections "
      "WHERE collectionID NOT IN (SELECT collectionID FROM deletedCollections)";
  if (auto r = forEachRow(
          db,
          "SELECT c.libraryID, c.key, c.collectionName, p.key FROM collections c "
          "LEFT JOIN collections p ON p.collectionID = c.parentCollectionID "
          "WHERE c.collectionID IN (" + liveCollections + ") "
          "ORDER BY c.libraryID, c.collectionName COLLATE NOCASE",
          "collections",
          [&](SqliteStatement &s) {
            snap.collections.append({s.columnInt(0), s.columnText(1),
                                     s.columnText(2), s.columnText(3)});
          });
      !r)
    return fail(r.error());

  // PDF attachments out of the trash (neither them nor their parent).
  QList<PdfRow> rows;
  if (auto r = forEachRow(
          db,
          "SELECT a.itemID, ai.key, a.parentItemID, pi.key, a.linkMode, a.path, "
          "a.storageHash, ai.libraryID, ai.dateModified, pi.dateModified "
          "FROM itemAttachments a "
          "JOIN items ai ON ai.itemID = a.itemID "
          "LEFT JOIN items pi ON pi.itemID = a.parentItemID "
          "WHERE (a.contentType = 'application/pdf' OR lower(a.path) LIKE '%.pdf') "
          "AND a.linkMode IN (0, 1, 2) "
          "AND a.itemID NOT IN (SELECT itemID FROM deletedItems) "
          "AND (a.parentItemID IS NULL "
          "     OR a.parentItemID NOT IN (SELECT itemID FROM deletedItems)) "
          "AND ai.libraryID IN (SELECT libraryID FROM libraries "
          "                     WHERE type IN ('user', 'group'))",
          "attachments",
          [&](SqliteStatement &s) {
            PdfRow row;
            const bool standalone = s.columnIsNull(2);
            row.metaItemId = standalone ? s.columnInt(0) : s.columnInt(2);
            row.linkMode = s.columnInt(4);
            row.rawPath = s.columnText(5);
            row.pdf.attachmentKey = s.columnText(1);
            row.pdf.itemKey = standalone ? s.columnText(1) : s.columnText(3);
            row.pdf.storageHash = s.columnText(6).toLower();
            row.pdf.libraryId = s.columnInt(7);
            row.pdf.dateModified = std::max(s.columnText(8), s.columnText(9));
            rows.append(row);
          });
      !r)
    return fail(r.error());

  // Data of every item at once: one query per kind, not per item.
  QHash<int, QHash<QString, QString>> fields;
  if (auto r = forEachRow(
          db,
          "SELECT d.itemID, f.fieldName, v.value FROM itemData d "
          "JOIN fields f ON f.fieldID = d.fieldID "
          "JOIN itemDataValues v ON v.valueID = d.valueID "
          "WHERE f.fieldName IN ('title', 'date', 'publicationTitle', "
          "'bookTitle', 'proceedingsTitle', 'DOI')",
          "item fields",
          [&](SqliteStatement &s) {
            fields[s.columnInt(0)].insert(s.columnText(1), s.columnText(2));
          });
      !r)
    return fail(r.error());

  QHash<int, QList<Creator>> creators;
  if (auto r = forEachRow(
          db,
          "SELECT ic.itemID, c.firstName, c.lastName, c.fieldMode, t.creatorType "
          "FROM itemCreators ic JOIN creators c ON c.creatorID = ic.creatorID "
          "JOIN creatorTypes t ON t.creatorTypeID = ic.creatorTypeID "
          "ORDER BY ic.itemID, ic.orderIndex",
          "creators",
          [&](SqliteStatement &s) {
            // fieldMode 1: a single-field name (an institution).
            const QString name = s.columnInt(3) == 1
                                     ? s.columnText(2)
                                     : (s.columnText(1) + " " + s.columnText(2));
            creators[s.columnInt(0)].append(
                {name.simplified(), s.columnText(4) == "author"});
          });
      !r)
    return fail(r.error());

  QHash<int, QString> itemTypes;
  if (auto r = forEachRow(
          db,
          "SELECT i.itemID, t.typeName FROM items i "
          "JOIN itemTypes t ON t.itemTypeID = i.itemTypeID",
          "item types",
          [&](SqliteStatement &s) { itemTypes.insert(s.columnInt(0), s.columnText(1)); });
      !r)
    return fail(r.error());

  QHash<int, QStringList> tags;
  if (auto r = forEachRow(
          db,
          "SELECT it.itemID, t.name FROM itemTags it "
          "JOIN tags t ON t.tagID = it.tagID ORDER BY t.name COLLATE NOCASE",
          "tags",
          [&](SqliteStatement &s) { tags[s.columnInt(0)].append(s.columnText(1)); });
      !r)
    return fail(r.error());

  QHash<int, QStringList> memberOf;
  if (auto r = forEachRow(
          db,
          "SELECT ci.itemID, c.key FROM collectionItems ci "
          "JOIN collections c ON c.collectionID = ci.collectionID "
          "WHERE c.collectionID IN (" + liveCollections + ")",
          "collection items",
          [&](SqliteStatement &s) { memberOf[s.columnInt(0)].append(s.columnText(1)); });
      !r)
    return fail(r.error());

  for (PdfRow &row : rows) {
    ZoteroPdf &pdf = row.pdf;
    const auto &f = fields.value(row.metaItemId);
    pdf.meta.title = f.value("title");
    pdf.meta.year = yearOf(f.value("date"));
    for (const char *name : {"publicationTitle", "bookTitle", "proceedingsTitle"})
      if (pdf.meta.journal.isEmpty())
        pdf.meta.journal = f.value(name);
    pdf.meta.doi = f.value("DOI");
    pdf.meta.itemType = itemTypes.value(row.metaItemId);
    pdf.meta.tags = tags.value(row.metaItemId);

    // The authors; without any, every creator (editors of a book...).
    const auto &all = creators.value(row.metaItemId);
    for (const Creator &c : all)
      if (c.isAuthor)
        pdf.meta.authors.append(c.name);
    if (pdf.meta.authors.isEmpty())
      for (const Creator &c : all)
        pdf.meta.authors.append(c.name);

    pdf.collectionKeys = memberOf.value(row.metaItemId);

    // storage:file.pdf → <data>/storage/<attachment key>/file.pdf;
    // attachments:a/b.pdf → <base>/a/b.pdf; otherwise an absolute path.
    const QString &p = row.rawPath;
    if (row.linkMode == kImportedFile || row.linkMode == kImportedUrl) {
      if (p.startsWith("storage:"))
        pdf.path = m_dataDir + "/storage/" + pdf.attachmentKey + "/" + p.mid(8);
    } else if (row.linkMode == kLinkedFile) {
      if (p.startsWith("attachments:")) {
        if (!m_baseAttachmentPath.isEmpty())
          pdf.path = m_baseAttachmentPath + "/" + p.mid(12);
      } else if (QDir::isAbsolutePath(QDir::fromNativeSeparators(p))) {
        pdf.path = p;
      }
    }
    if (!pdf.path.isEmpty()) {
      pdf.path = QDir::cleanPath(QDir::fromNativeSeparators(pdf.path));
      pdf.onDisk = QFileInfo(pdf.path).isFile();
    }
    snap.pdfs.append(std::move(pdf));
  }
  return snap;
}
