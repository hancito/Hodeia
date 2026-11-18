#pragma once

#include "libraryrepository.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QVariant>

class SqliteDb;

/// Model over the Citations table (joined to Summaries) that backs the
/// references display.  It also owns the APA formatting so QML only renders a
/// ready-to-show string.
class CitationsModel : public QAbstractListModel {
  Q_OBJECT

public:
  enum Roles {
    PaperHashRole = Qt::UserRole + 1,
    CitesCountRole,
    CitedByCountRole
  };

  explicit CitationsModel(QObject *parent = nullptr);
  void setDatabase(SqliteDb &db);

  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  // Bumped by loadAll().  A QML binding that reads a computed display string
  // via the invokable referencesDisplayForHash() has no trackable dependency
  // on the model data, so it depends on this property to re-evaluate after a
  // reload.
  Q_PROPERTY(int version READ version NOTIFY versionChanged)
  int version() const { return m_version; }

  /// Reload all rows (joined to Summaries so only summarized papers appear).
  Q_INVOKABLE void loadAll();

  /// Build the APA-formatted references display (markdown) for @p paperHash.
  /// Returns an empty string if the paper has no citation row.  Both the
  /// "References" (works this paper cited) and "Cited by" (works that cite it)
  /// sections are produced here, with the true OpenAlex totals in the headers.

  Q_INVOKABLE QString citesDisplayForHash(const QString &paperHash) const;
  Q_INVOKABLE QString citedByDisplayForHash(const QString &paperHash) const;

signals:
  void versionChanged();

private:
  const CitationRow *findByHash(const QString &paperHash) const;
  QString formatSection(const QString &header, const QString &itemsJson,
                        int total) const;

  SqliteDb *m_db = nullptr;
  QVector<CitationRow> m_rows;
  int m_version = 0;
};
