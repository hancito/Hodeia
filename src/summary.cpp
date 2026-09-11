#include "summary.h"

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
  case CitesCountRole:
    return row.citesCount;
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
  roles[CitesCountRole] = "citesCount";
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

  // LEFT JOIN so papers without citation data still appear (cites_count 0).
  SqliteStatement stmt = m_db->prepare(
      "SELECT s.id, s.paper_hash, s.title, s.authors, s.pages_num, s.journal, "
      "s.year, s.topics, s.llmodel, s.summary_json, s.date, "
      "COALESCE(c.cites_count, 0) "
      "FROM Summaries s LEFT JOIN Citations c ON c.paper_hash = s.paper_hash "
      "ORDER BY s.id");

  while (true) {
    auto stepResult = stmt.step();
    if (!stepResult || !stepResult.value())
      break;
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
    row.citesCount = stmt.columnInt(11);
    m_rows.append(row);
  }

  endResetModel();
}
