#pragma once

#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QThread>

class ConfigManager;
class DatabaseManager;
class QNetworkAccessManager;
class QNetworkReply;
class QUrlQuery;

/// Independent background service that fetches citation data from the
/// OpenAlex API and stores it in the Citations table.
///
/// For each paper it:
///   1. Finds the OpenAlex work by semantic title search (stores work_id).
///   2. Fetches the works it cites (its references)  -> Citations.cites
///   3. Fetches the works that cite it (citing papers) -> Citations.cited_by
///
/// Lives in its own QThread.  Triggered by:
///   - SummaryController::summaryFinished()  (after a summarization batch)
///   - checkEmptyReferences() on app startup (catch-up scan)
class OpenAlexClient : public QObject {
  Q_OBJECT

public:
  explicit OpenAlexClient(ConfigManager *config, DatabaseManager *database,
                          QObject *parent = nullptr);
  ~OpenAlexClient() override;

  /// Scan the Citations table for rows missing citation data and fetch it
  /// from OpenAlex.  Safe to call from any thread (queued).
  Q_INVOKABLE void checkEmptyReferences();

signals:
  void referencesSaved(const QString &paperHash);

private:
  // Which citation list we are fetching: works this paper cited (cites) or
  // works that cited it (cited_by).
  enum class RefKind { Cites, CitedBy };

  void fetchAndSave(const QString &paperHash, const QString &title);
  void handleSearchReply(QNetworkReply *reply, const QString &paperHash);
  void fetchWorkList(const QString &paperHash, RefKind kind,
                     const QString &workId);
  void handleListReply(QNetworkReply *reply, const QString &paperHash,
                       RefKind kind, const QString &workId, int page);
  void saveWorkList(const QString &paperHash, RefKind kind,
                    const QString &itemsJson);
  void appendPage(QUrlQuery &query, int page) const;
  void appendAuthQuery(QUrlQuery &query) const;

  ConfigManager *m_config = nullptr;
  DatabaseManager *m_database = nullptr;
  QNetworkAccessManager *m_nam = nullptr;
  QNetworkReply *m_pending = nullptr;
  QThread *m_thread = nullptr;
  // Work items accumulated for the list currently being paged (worker thread).
  QJsonArray m_listBuffer;
};
