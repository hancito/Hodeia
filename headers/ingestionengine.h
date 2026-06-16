#pragma once
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

class PdfWorker : public QObject {
  Q_OBJECT
public:
  explicit PdfWorker(QObject *parent = nullptr);
  ~PdfWorker();

public slots:
  void processPDF(const QList<QUrl> &filePath, const QString &imgPath,
                  const QString &dbPath);
  void stop();

signals:
  void finished();

private:
  QString extractText(const QUrl &filePath, const QString &imgPath) const;
  QStringList multiExtractText(const QList<QUrl> &filePaths,
                               const QString &imgPath,
                               const QString &dbPath) const;

  QStringList parallelHasher(const QList<QUrl> &fileList) const;
};
