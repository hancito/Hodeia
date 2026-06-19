#include "headers/summary.h"
#include "headers/database.h"
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

SummaryModel::SummaryModel(QObject *parent) : QAbstractListModel(parent) {}

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
  case ModelRole:
    return row.model;
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
  return {{IdRole, "id"},
          {PaperHashRole, "paperHash"},
          {TitleRole, "title"},
          {AuthorsRole, "authors"},
          {PagesNumRole, "pagesNum"},
          {JournalRole, "journal"},
          {YearRole, "year"},
          {TopicsRole, "topics"},
          {ModelRole, "model"},
          {SummaryJsonRole, "summaryJson"},
          {DateRole, "date"},
          {FormattedDateRole, "formattedDate"}};
}

QString SummaryModel::formatDate(const QDateTime &dt) const {
  if (dt.isValid()) {
    // Convert to local time zone for display
    return dt.toLocalTime().toString("yyyy-MM-dd hh:mm:ss");
  }
  return {};
}

void SummaryModel::loadAll() {
  beginResetModel();
  m_rows.clear();

  QSqlQuery query(
      "SELECT id, paper_hash, title, authors, pages_num, journal, year, "
      "topics, model, summary_json, date FROM "
      "Summaries ORDER BY id",
      QSqlDatabase::database(ProjectDbConnectionName));

  while (query.next()) {
    SummaryRow row;
    row.id = query.value(0).toInt();
    row.paperHash = query.value(1).toString();
    row.title = query.value(2).toString();
    row.authors = query.value(3).toString();
    row.pagesNum = query.value(4).toInt();
    row.journal = query.value(5).toString();
    row.year = query.value(6).toString();
    row.topics = query.value(7).toString();
    row.model = query.value(8).toString();
    row.summaryJson = query.value(9);
    row.date = query.value(10).toDateTime();
    m_rows.append(row);
  }

  endResetModel();
}

void SummaryModel::insert(const SummaryRow &row) {
  beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size());

  SummaryRow newRow = row;
  m_rows.append(newRow);
  int newId = m_rows.size() - 1;

  endInsertRows();

  QSqlQuery query(QSqlDatabase::database(ProjectDbConnectionName));
  query.prepare(
      "INSERT INTO Summaries "
      "(paper_hash, title, authors, pages_num, journal, year, topics, model, "
      "summary_json, date) "
      "VALUES (:hash, :title, :authors, :pages, :journal, :year, :topics, "
      ":model, :json, :date)");
  query.bindValue(":hash", newRow.paperHash);
  query.bindValue(":title", newRow.title);
  query.bindValue(":authors", newRow.authors);
  query.bindValue(":pages", newRow.pagesNum);
  query.bindValue(":journal", newRow.journal);
  query.bindValue(":year", newRow.year);
  query.bindValue(":topics", newRow.topics);
  query.bindValue(":model", newRow.model);
  query.bindValue(":json", newRow.summaryJson);
  query.bindValue(":date", newRow.date);

  if (!query.exec()) {
    qDebug() << "Insert Summary failed:" << query.lastError().text();
  }
}

void SummaryModel::removeRow(int index) {
  if (index < 0 || index >= m_rows.size())
    return;

  int dbId = m_rows[index].id;
  beginRemoveRows(QModelIndex(), index, index);
  m_rows.removeAt(index);
  endRemoveRows();

  QSqlQuery query(QSqlDatabase::database(ProjectDbConnectionName));
  query.prepare("DELETE FROM Summaries WHERE id = :id");
  query.bindValue(":id", dbId);
  if (!query.exec()) {
    qDebug() << "Delete Summary failed:" << query.lastError().text();
  }
}

void SummaryModel::clear() {
  beginResetModel();
  m_rows.clear();
  endResetModel();

  QSqlQuery query(QSqlDatabase::database(ProjectDbConnectionName));
  query.exec("DELETE FROM Summaries");
  if (!query.exec()) {
    qDebug() << "Clear Summaries failed:" << query.lastError().text();
  }
}

SummaryRow SummaryModel::getRow(int index) const {
  if (index < 0 || index >= m_rows.size())
    return SummaryRow{};
  return m_rows[index];
}
