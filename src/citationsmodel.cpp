#include "citationsmodel.h"
#include "sqlitedb.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

CitationsModel::CitationsModel(QObject *parent) : QAbstractListModel(parent) {}

void CitationsModel::setDatabase(SqliteDb &db) { m_db = &db; }

int CitationsModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  return m_rows.size();
}

QVariant CitationsModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
    return {};
  const auto &row = m_rows[index.row()];
  switch (role) {
  case PaperHashRole:
    return row.paperHash;
  case CitesCountRole:
    return row.citesCount;
  case CitedByCountRole:
    return row.citedByCount;
  default:
    return {};
  }
}

QHash<int, QByteArray> CitationsModel::roleNames() const {
  QHash<int, QByteArray> roles;
  roles[PaperHashRole] = "paperHash";
  roles[CitesCountRole] = "citesCount";
  roles[CitedByCountRole] = "citedByCount";
  return roles;
}

void CitationsModel::loadAll() {
  if (!m_db)
    return;

  beginResetModel();
  m_rows.clear();

  // Only summarized papers (joined to Summaries), matching the paper list.
  SqliteStatement stmt =
      m_db->prepare("SELECT c.paper_hash, c.cites, c.cited_by, c.cites_count, "
                    "c.cited_by_count FROM Citations c JOIN Summaries s "
                    "ON s.paper_hash = c.paper_hash ORDER BY c.paper_hash");

  while (true) {
    auto stepResult = stmt.step();
    if (!stepResult || !stepResult.value())
      break;
    CitationRow row;
    row.paperHash = stmt.columnText(0);
    row.cites = stmt.columnText(1);
    row.citedBy = stmt.columnText(2);
    row.citesCount = stmt.columnInt(3);
    row.citedByCount = stmt.columnInt(4);
    m_rows.append(row);
  }

  endResetModel();

  // Notify QML bindings that read a computed display string (which do not
  // track the model's dataChanged/layoutChanged) that the data has changed.
  ++m_version;
  emit versionChanged();
}

const CitationRow *CitationsModel::findByHash(const QString &paperHash) const {
  for (const auto &row : m_rows)
    if (row.paperHash == paperHash)
      return &row;
  return nullptr;
}

QString CitationsModel::citedByDisplayForHash(const QString &paperHash) const {
  const CitationRow *row = findByHash(paperHash);
  if (!row)
    return {};

  // "References" = works this paper cited (citedBy column).
  // "Cited by"   = works that cite it (cites column).
  QString out;
  out += formatSection(tr("Cited by"), row->cites, row->citesCount);
  return out;
}

QString CitationsModel::citesDisplayForHash(const QString &paperHash) const {
  const CitationRow *row = findByHash(paperHash);
  if (!row)
    return {};

  // "References" = works this paper cited (citedBy column).
  // "Cited by"   = works that cite it (cites column).
  QString out;
  out += formatSection(tr("References"), row->citedBy, row->citedByCount);
  return out;
}

// APA-style: "Authors (Year). Title. Journal. https://doi.org/DOI"
// Authors in the stored string are joined with \u001F (unit separator) because
// each name itself contains a comma ("Family, Given").
static QString apaAuthors(const QString &raw) {
  if (raw.isEmpty())
    return {};
  const QStringList parts = raw.split(QChar(0x1F));
  QStringList names;
  for (const QString &s : parts) {
    const QString t = s.trimmed();
    if (!t.isEmpty())
      names << t;
  }
  if (names.isEmpty())
    return {};
  if (names.size() == 1)
    return names.first();
  const QString head = names.mid(0, names.size() - 1).join(", ");
  return head + " & " + names.last();
}

QString CitationsModel::formatSection(const QString &header,
                                      const QString &itemsJson,
                                      int total) const {
  QString out;
  out += "## ";
  out += header + " ( ";

  // Show the true OpenAlex total in the header; note when the list is capped.
  QJsonArray arr = QJsonDocument::fromJson(itemsJson.toUtf8()).array();
  const int shown = arr.size();
  if (total > 0) {
    out += QString::number(total) + " ) " + "\n\n";
    if (shown < total)
      out += tr(" ( Showing top %1 ) ").arg(shown) + "\n\n";
  }

  if (arr.isEmpty()) {
    out += tr("None found on OpenAlex") + " ) \n\n";
    return out;
  }

  for (const QJsonValue &v : arr) {
    const QJsonObject r = v.toObject();
    const QString authors = apaAuthors(r.value("authors").toString());
    QString line = "- ";
    if (!authors.isEmpty())
      line += authors + " ";
    const int year = r.value("publication_year").toInt();
    if (year > 0)
      line += "(" + QString::number(year) + "). ";
    line += r.value("display_name").toString().isEmpty()
                ? tr("Unknown")
                : r.value("display_name").toString() + ".";
    const QString journal = r.value("journal").toString();
    if (!journal.isEmpty())
      line += " " + journal + ".";
    const QString doi = r.value("doi").toString();
    if (!doi.isEmpty()) {
      // Render as a Markdown link so the references tab shows a clickable
      // DOI.  OpenAlex returns a full URL; if a bare DOI is stored instead,
      // prefix the canonical resolver so it still resolves.
      const QString url =
          doi.startsWith("http://", Qt::CaseInsensitive) ||
                  doi.startsWith("https://", Qt::CaseInsensitive)
              ? doi
              : QStringLiteral("https://doi.org/") + doi;
      line += " [" + doi + "](" + url + ")";
    }
    out += line + "\n";
  }
  return out;
}
