#pragma once

#include "headers/summarizer.h"
#include <QObject>
#include <QString>
#include <QThread>

class SummaryController : public QObject {
  Q_OBJECT

public:
  explicit SummaryController(ConfigManager *config, DatabaseManager *database,
                             QObject *parent = nullptr);
  ~SummaryController() override;

public slots:
  void startSummarize(const QStringList &hashes);
  void stopSummarize();

signals:
  void summaryStarted();
  void summaryFinished();
  void summaryStopped();
  void paperFinished(const QString &hash, bool success, const QString &message);
  void allFinished(int successCount, int total);

private:
  QThread *m_thread = nullptr;
  Summarizer *m_worker = nullptr;
  QStringList m_hashes;
  DatabaseManager *m_database = nullptr;
  ConfigManager *m_config = nullptr;
};
