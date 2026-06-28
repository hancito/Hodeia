#pragma once

#include "headers/summarizer.h"
#include <QObject>
#include <QString>
#include <QThread>

class SummaryController : public QObject {
  Q_OBJECT

public:
  explicit SummaryController(QObject *parent = nullptr);
  ~SummaryController() override;

public slots:
  void startSummarize();
  void stopSummarize();

signals:
  void summaryStarted();
  void summaryFinished();

private:
  QThread *m_thread = nullptr;
  Summarizer *m_worker = nullptr;
  QString m_dbPath;
  QString m_imgPath;
  ConfigManager *m_config = nullptr;
};
