#include "headers/summary.h"
#include <QVariantMap>

SummaryModel::SummaryModel(QObject *parent) : QAbstractListModel(parent) {}

void SummaryModel::setDatabase(SqliteDb &db) { m_db = &db; }

int SummaryModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  return m_rows.size();
}

QVariant SummaryModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
    return {};
  const auto &row = m_rows[index.row()];

  switch (role) {
  case IdRole:
    return row.id;
  case PaperHashRole:
    return row.paperHash;
  case TitleRole:
    return row.title;
  case AuthorsRole:
    return row.authors;
  case PagesNumRole:
    return row.pagesNum;
  case JournalRole:
    return row.journal;
  case YearRole:
    return row.year;
  case TopicsRole:
    return row.topics;
  case LlmModelRole:
    return row.llmodel;
  case SummaryJsonRole:
    return row.summaryJson;
  case DateRole:
    return row.date;
  case FormattedDateRole:
    return formatDate(row.date);
  default:
    return {};
  }
}

QHash<int, QByteArray> SummaryModel::roleNames() const {
  QHash<int, QByteArray> roles;
  roles[IdRole] = "id";
  roles[PaperHashRole] = "paperHash";
  roles[TitleRole] = "title";
  roles[AuthorsRole] = "authors";
  roles[PagesNumRole] = "pagesNum";
  roles[JournalRole] = "journal";
  roles[YearRole] = "year";
  roles[TopicsRole] = "topics";
  roles[LlmModelRole] = "llmodel";
  roles[SummaryJsonRole] = "summaryJson";
  roles[DateRole] = "date";
  roles[FormattedDateRole] = "formattedDate";
  return roles;
}

QString SummaryModel::formatDate(const QDateTime &dt) const {
  if (dt.isValid()) {
    return dt.toLocalTime().toString("yyyy-MM-dd hh:mm:ss");
  }
  return {};
}

void SummaryModel::loadAll() {
  if (!m_db)
    return;

  beginResetModel();
  m_rows.clear();

  SqliteStatement stmt = m_db->prepare(
      "SELECT id, paper_hash, title, authors, pages_num, journal, "
      "year, topics, llmodel, summary_json, date FROM Summaries "
      "ORDER BY id");

  while (stmt.step()) {
    SummaryRow row;
    row.id = stmt.columnInt(0);
    row.paperHash = stmt.columnText(1);
    row.title = stmt.columnText(2);
    row.authors = stmt.columnText(3);
    row.pagesNum = stmt.columnInt(4);
    row.journal = stmt.columnText(5);
    row.year = stmt.columnText(6);
    row.topics = stmt.columnText(7);
    row.llmodel = stmt.columnText(8);
    row.summaryJson = stmt.columnBlob(9);
    row.date = stmt.columnDateTime(10);
    m_rows.append(row);
  }

  endResetModel();
}

void SummaryModel::insert(const SummaryRow &row) {
  if (!m_db)
    return;

  beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size());
  SummaryRow newRow = row;
  m_rows.append(newRow);
  endInsertRows();

  SqliteStatement stmt =
      m_db->prepare("INSERT INTO Summaries "
                    "(paper_hash, title, authors, pages_num, journal, year, "
                    "topics, llmodel, summary_json, date) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");

  stmt.bind(1, newRow.paperHash);
  stmt.bind(2, newRow.title);
  stmt.bind(3, newRow.authors);
  stmt.bind(4, newRow.pagesNum);
  stmt.bind(5, newRow.journal);
  stmt.bind(6, newRow.year);
  stmt.bind(7, newRow.topics);
  stmt.bind(8, newRow.llmodel);
  stmt.bind(9, newRow.summaryJson.toByteArray());
  stmt.bind(10, newRow.date);

  if (!stmt.exec()) {
    qDebug() << "Insert Summary failed:" << stmt.errorMsg();
  }
}

void SummaryModel::removeRow(int index) {
  if (!m_db || index < 0 || index >= m_rows.size())
    return;

  int dbId = m_rows[index].id;
  beginRemoveRows(QModelIndex(), index, index);
  m_rows.removeAt(index);
  endRemoveRows();

  SqliteStatement stmt = m_db->prepare("DELETE FROM Summaries WHERE id = ?");
  stmt.bind(1, dbId);
  if (!stmt.exec()) {
    qDebug() << "Delete Summary failed:" << stmt.errorMsg();
  }
}

void SummaryModel::clear() {
  if (!m_db)
    return;

  beginResetModel();
  m_rows.clear();
  endResetModel();

  m_db->exec("DELETE FROM Summaries");
}

SummaryRow SummaryModel::getRow(int index) const {
  if (index < 0 || index >= m_rows.size())
    return SummaryRow{};
  return m_rows[index];
}
