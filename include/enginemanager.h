#pragma once
#include "ingestionengine.h"
#include <QObject>
#include <QThread>
#include <QUrl>

class ConfigManager;
class SqliteVecManager;

class EngineController : public QObject {
  Q_OBJECT
public:
  explicit EngineController(ConfigManager *config, SqliteVecManager *vecManager,
                            QObject *parent = nullptr);
  ~EngineController() override;

public slots:
  void startEngine(const QList<QUrl> &pdfFiles, const QString &imgPath,
                   const QString &dbPath);
  void stopEngine();
  /// User cancel: returns immediately; the worker stops at its next safe
  /// point, then emits ingestionReport and ingestionFinished as usual.
  void cancelEngine();
  QList<QUrl> findPdfFiles(const QUrl &folderPath) const;

signals:
  void ingestionFinished();
  void ingestionStarted();
  void ingestionStatus(QString statusLog);
  /// Per-operation progress for the import window (see
  /// PdfWorker::stepProgress).
  void ingestionStepProgress(const QString &step, int current, int total,
                             const QString &detail);
  void ingestionPapersProgress(int done, int total);
  /// End-of-import summary for a message window (see PdfWorker::report).
  void ingestionReport(const QString &title, const QString &message,
                       bool isError);
  /// The project database the import writes to, when it starts; empty
  /// once its thread has stopped (nothing more is written).
  void ingestionTargetChanged(const QString &dbPath);

private:
  void releaseAndReloadModel();

  QThread *m_thread = nullptr;
  PdfWorker *m_worker = nullptr;
  ConfigManager *m_config = nullptr;
  SqliteVecManager *m_vecManager = nullptr;
};
