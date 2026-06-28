#pragma once

#include <QObject>
#include <QStringList>

class ConfigManager;
class DatabaseManager;
class LocalBackend;
class QThread;

/// Paper-level summary produced by the LLM, mirrors the Python PaperSummary
/// schema.
struct PaperSummary {
  QString title;
  QStringList authors;
  QString journal;
  int year = 0;
  QStringList topics;
  QString researchObjective;
  QString methodology;
  QString coreFindingsAndSignificance;
  QString criticalLimitations;
  QString citableSummary;
};

/// Central orchestrator for paper summarization.
///
/// The heavy lifting (chunk retrieval, LLM calls, DB writes) runs in a
/// dedicated worker thread so the main thread stays responsive.
///
/// Model discovery has been moved to ModelHelper (helpers.h).
///
/// Usage from QML:
///   summarizer.summarizePapers(["hash1", "hash2"])
class Summarizer : public QObject {
  Q_OBJECT

public:
  explicit Summarizer(ConfigManager *config, DatabaseManager *database,
                      QObject *parent = nullptr);
  ~Summarizer() override;

public slots:
  void summarizePapers(const QStringList &hashes);
  void stop();

signals:
  void finished();
  void started();

private:
  QString retrieveContext(const QString &paperHash) const;
  /// Get the system prompt template sent to the LLM.
  static const QString &systemPrompt();

  ConfigManager *m_config = nullptr;
  DatabaseManager *m_database = nullptr;
  LocalBackend *m_localBackend = nullptr;

  // Worker thread for backend inference
  QThread *m_workerThread = nullptr;

  bool m_isBusy = false;
};
