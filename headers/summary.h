#pragma once
#include "headers/sqlitedb.h"
#include <QAbstractListModel>
#include <QDateTime>
#include <QString>
#include <QVariant>

struct TextChunk {
  QString text;
  QString paperHash;
  int page = 0;
  int chunkIdx = 0;
};

struct SummaryRow {
  int id = 0;
  QString paperHash;
  QString title;
  QString authors;
  int pagesNum = 0;
  QString journal;
  QString year;
  QString topics;
  QString llmodel;
  QVariant summaryJson;
  QDateTime date;
};

struct ExtractionResult {
  int pageCount = 0;
  QString paperHash;
  QString title;
  QList<TextChunk> chunks;
};

class SummaryModel : public QAbstractListModel {
  Q_OBJECT

public:
  enum Roles {
    IdRole = Qt::UserRole + 1,
    PaperHashRole,
    TitleRole,
    AuthorsRole,
    PagesNumRole,
    JournalRole,
    YearRole,
    TopicsRole,
    LlmModelRole,
    SummaryJsonRole,
    DateRole,
    FormattedDateRole
  };

  explicit SummaryModel(QObject *parent = nullptr);
  void setDatabase(SqliteDb &db);

  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE void loadAll();
  Q_INVOKABLE void insert(const SummaryRow &row);
  Q_INVOKABLE void removeRow(int index);
  Q_INVOKABLE void clear();

  SummaryRow getRow(int index) const;
  QString formatDate(const QDateTime &dt) const;

private:
  SqliteDb *m_db = nullptr;
  QVector<SummaryRow> m_rows;
};
