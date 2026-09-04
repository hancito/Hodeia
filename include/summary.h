#pragma once
#include "sqlitedb.h"
#include <QAbstractListModel>
#include <QDateTime>
#include <QString>
#include <QVariant>

struct TextChunk {
  QString text;
  QString paperHash;
  int page = 0;
  int chunkIdx = 0;
  QString cleanTitle;
  QString citation;
  QString pageMetadata; ///< JSON metadata: figures, tables, scanned, etc.
};

/// Per-page metadata detected during PDF extraction.
struct PageMetadata {
  int page = 0;
  bool hasFigure = false;         ///< Page contains at least one image block
  int figureCount = 0;            ///< Number of embedded images
  bool hasVectorGraphics = false; ///< Vector graphics (charts, diagrams)
  bool hasTable = false;  ///< Page contains table-like structure (heuristic)
  bool isScanned = false; ///< Page was OCR'd by Tesseract fallback
  int ocrChars = 0;       ///< Characters produced by OCR (0 = native text)
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
  QVector<PageMetadata> pageMetadata; ///< Per-page figure/table/scanned flags
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

private:
  SqliteDb *m_db = nullptr;
  QVector<SummaryRow> m_rows;
  QString formatDate(const QDateTime &dt) const;
};
