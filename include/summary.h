#pragma once

#include "libraryrepository.h"
#include "sqlitedb.h"
#include "textchunk.h"
#include <QAbstractListModel>
#include <QDateTime>
#include <QString>
#include <QVariant>

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

struct ExtractionResult {
  int pageCount = 0;
  QString paperHash;
  QString title;
  QList<TextChunk> chunks;
  QVector<PageMetadata> pageMetadata; ///< Per-page figure/table/scanned flags
  QString fileName; ///< Source file name, for user-facing messages
  QString error;    ///< Why the file could not be read; empty on success
};

class SummaryModel : public QAbstractListModel {
  Q_OBJECT
  // Qt::UserRole is 256 so IdRole is 257 and so on
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
    FormattedDateRole,
    CitesCountRole
  };

  explicit SummaryModel(QObject *parent = nullptr);
  void setDatabase(SqliteDb &db);

  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE void loadAll();

  // Bumped by loadAll(). QML bindings that read rows through functions
  // (rowCount()/get()) are not notified by a model reset; reading `version`
  // makes them re-evaluate (same pattern as CitationsModel).
  Q_PROPERTY(int version READ version NOTIFY versionChanged)
  int version() const { return m_version; }

signals:
  void versionChanged();

private:
  int m_version = 0;
  SqliteDb *m_db = nullptr;
  QVector<SummaryRow> m_rows;
  QString formatDate(const QDateTime &dt) const;
};
