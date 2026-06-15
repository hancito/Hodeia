#pragma once
#include <QObject>
#include <QString>
#include <QUrl>

class PdfWorker : public QObject {
  Q_OBJECT
public:
  explicit PdfWorker(QObject *parent = nullptr);
  ~PdfWorker();

Q_INVOKABLE public slots:
  void processPDF(const QString &filePath, const QString &dbPath) const;
  QString getFileHash(const QUrl &filePath) const;
  QString extractText(const QUrl &filePath);
};
