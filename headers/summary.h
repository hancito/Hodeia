#pragma once
#include <QAbstractListModel>
#include <QDateTime>
#include <QString>
#include <QVariant>

struct SummaryRow {
  int id = 0;
  QString paperHash;
  QString title;
  QString authors;
  int pagesNum = 0;
  QString journal;
  QString year;
  QString topics;
  QString model;
  QVariant summaryJson;
  QDateTime date;
};

struct ExtractionResult {
  QString extractedText;
  int pageCount = 0;
  QString paperHash;
  QString title;
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
    ModelRole,
    SummaryJsonRole,
    DateRole,
    FormattedDateRole
  };

  explicit SummaryModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE void loadAll();
  Q_INVOKABLE void insert(const SummaryRow &row);
  Q_INVOKABLE void removeRow(int index);
  Q_INVOKABLE void clear();

  SummaryRow getRow(int index) const;

private:
  QVector<SummaryRow> m_rows;

  QString formatDate(const QDateTime &dt) const;
};
