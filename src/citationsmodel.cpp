#include "citationsmodel.h"
#include "citationformat.h"
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

  m_rows = LibraryRepository(*m_db).citations();

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

  for (const QJsonValue &v : arr)
    out += "- " + CitationFormat::apaReference(v.toObject(), tr("Unknown")) +
           "\n";
  return out;
}
