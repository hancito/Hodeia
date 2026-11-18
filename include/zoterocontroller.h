#pragma once

#include "zoterocollectionmodel.h"
#include "zoterorepository.h"
#include "zoterosource.h"

#include <QObject>
#include <QVariantList>

class ConfigManager;
class DatabaseManager;
class EngineController;

/// The Zotero connector for QML (zoteroController): finds Zotero, reads it,
/// keeps the project's Zotero tables in step, proposes the new PDFs of the
/// followed collections and imports the chosen ones through the usual
/// ingestion.  HodeIA never writes to Zotero.
///
/// Reading Zotero and hashing the new PDFs run on a worker thread; the
/// project database is written on the GUI thread, like the rest of the app.
class ZoteroController : public QObject {
  Q_OBJECT
  Q_PROPERTY(ZoteroCollectionModel *collections READ collections CONSTANT)
  /// Zotero's data folder in use; empty when not found.
  Q_PROPERTY(QString dataDir READ dataDir NOTIFY stateChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
  /// Last error for the user (reading Zotero, writing the project).
  Q_PROPERTY(QString error READ error NOTIFY stateChanged)
  /// PDFs to propose: {id, title, authors, year, journal, collections}.
  Q_PROPERTY(QVariantList proposals READ proposals NOTIFY stateChanged)
  /// PDFs of the followed collections that Zotero has not downloaded.
  Q_PROPERTY(int missingCount READ missingCount NOTIFY stateChanged)

public:
  ZoteroController(ConfigManager *config, DatabaseManager *database,
                   EngineController *engine, QObject *parent = nullptr);

  ZoteroCollectionModel *collections() { return &m_collections; }
  QString dataDir() const { return m_dataDir; }
  bool busy() const { return m_busy; }
  QString error() const { return m_error; }
  QVariantList proposals() const { return m_proposals; }
  int missingCount() const { return m_missingCount; }

  /// Read Zotero again and bring the project up to date.
  Q_INVOKABLE void refresh();
  /// Use another data folder (a path or a file:// URL).  Returns an error
  /// message, or an empty string when the folder holds a zotero.sqlite.
  Q_INVOKABLE QString setDataDir(const QString &pathOrUrl);
  /// Follow the collections checked in `collections`, then refresh.
  Q_INVOKABLE void saveCollections();
  /// Import proposed PDFs (ZoteroItems ids) with the usual ingestion.
  Q_INVOKABLE void importProposals(const QVariantList &ids);
  /// Never propose these PDFs again.
  Q_INVOKABLE void ignoreProposals(const QVariantList &ids);

signals:
  void stateChanged();

private:
  struct Loaded {
    QString error;
    ZoteroSnapshot snapshot;
    QHash<QString, QString> hashes; ///< by ZoteroSync::pdfId
  };

  void setBusy(bool busy);
  void fail(const QString &message);
  /// Worker-thread part of refresh(): read Zotero, hash the new PDFs.
  static Loaded load(const QString &dataDir, const QString &baseAttachmentPath,
                     const QList<FollowedCollection> &followed,
                     const QList<ZoteroItemRow> &known);
  /// GUI-thread part: write the plan, rebuild what QML shows.
  void apply(const Loaded &loaded);
  void rebuildView();
  QSet<QString> indexedHashes() const;

  ConfigManager *m_config;
  DatabaseManager *m_database;
  EngineController *m_engine;
  ZoteroCollectionModel m_collections;

  QString m_dataDir;
  bool m_busy = false;
  bool m_refreshAgain = false; ///< a refresh was asked while one was running
  bool m_importing = false;    ///< an import of ours is running
  QString m_error;
  QString m_loadedDbPath; ///< project the running load was started for
  ZoteroSnapshot m_snapshot;
  QVariantList m_proposals;
  int m_missingCount = 0;
};
