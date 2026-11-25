// PdfExtractor on generated PDFs: text pages, a page without text (OCR
// fallback), and damaged files that make MuPDF throw (its errors are
// setjmp/longjmp: they must not skip C++ destructors — run under ASan).

#include "pdfextractor.h"

#include <QDir>
#include <QFile>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QTest>

class TestPdfExtractor : public QObject {
  Q_OBJECT

private slots:
  void textPages();
  void pageWithoutText();
  void damagedFile();
  void truncatedFile();

private:
  QString writePdf(const QString &name, const QStringList &pages,
                   bool imageOnly = false);
  ExtractionResult extract(const QString &path);

  QTemporaryDir m_dir;
};

QString TestPdfExtractor::writePdf(const QString &name,
                                   const QStringList &pages, bool imageOnly) {
  const QString path = m_dir.filePath(name);
  QPdfWriter writer(path);
  writer.setResolution(72);
  QPainter painter(&writer);
  for (int i = 0; i < pages.size(); ++i) {
    if (i > 0)
      writer.newPage();
    if (imageOnly) {
      QImage img(400, 300, QImage::Format_RGB888);
      img.fill(Qt::white);
      QPainter(&img).fillRect(50, 50, 300, 200, Qt::darkBlue);
      painter.drawImage(QPoint(40, 40), img);
    } else {
      painter.drawText(QRect(40, 40, 500, 700), Qt::TextWordWrap, pages[i]);
    }
  }
  painter.end();
  return path;
}

ExtractionResult TestPdfExtractor::extract(const QString &path) {
  const PdfExtractor extractor(nullptr); // no model: token estimates
  IngestionJob job;
  job.url = QUrl::fromLocalFile(path);
  job.paperHash = PdfExtractor::fileHash(job.url);
  return extractor.extract(job, m_dir.path());
}

void TestPdfExtractor::textPages() {
  const QString lorem =
      "The quick brown fox jumps over the lazy dog while the committee "
      "reviews the hybrid choice model and its latent variables. ";
  const QString path =
      writePdf("text.pdf", {"Page one. " + lorem.repeated(3),
                            "Page two. " + lorem.repeated(3)});
  const ExtractionResult r = extract(path);
  QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
  QCOMPARE(r.pageCount, 2);
  QCOMPARE(r.pageMetadata.size(), 2);
  QVERIFY(!r.pageMetadata[0].isScanned);
  QVERIFY(!r.chunks.isEmpty());
  QString all;
  for (const TextChunk &c : r.chunks)
    all += c.text + " ";
  QVERIFY(all.contains("Page one"));
  QVERIFY(all.contains("Page two"));
  // One PNG per page, named after the paper.
  for (int page = 1; page <= 2; ++page)
    QVERIFY(QFile::exists(QDir(m_dir.path()).filePath(
        QString("%1_page_%2.png").arg(r.paperHash).arg(page, 3, 10, QChar('0')))));
}

void TestPdfExtractor::pageWithoutText() {
  // No text layer: the OCR fallback reads the page (text drawn as an image).
  const QString path = m_dir.filePath("scan.pdf");
  {
    QPdfWriter writer(path);
    writer.setResolution(72);
    QPainter painter(&writer);
    QImage img(1200, 400, QImage::Format_RGB888);
    img.fill(Qt::white);
    QPainter p(&img);
    QFont font("DejaVu Sans");
    font.setPixelSize(48);
    p.setFont(font);
    p.setPen(Qt::black);
    p.drawText(QRect(20, 20, 1160, 360), Qt::TextWordWrap,
               "Scanned page about latent variables and choice models");
    p.end();
    painter.drawImage(QRect(40, 40, 500, 167), img);
  }
  const ExtractionResult r = extract(path);
  QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
  QCOMPARE(r.pageCount, 1);
  QCOMPARE(r.pageMetadata.size(), 1);
  QVERIFY(r.pageMetadata[0].isScanned);
  QVERIFY(r.pageMetadata[0].hasFigure);
  QVERIFY(r.pageMetadata[0].ocrChars > 0);
  QString all;
  for (const TextChunk &c : r.chunks)
    all += c.text + " ";
  QVERIFY2(all.contains("choice", Qt::CaseInsensitive), qPrintable(all));
}

void TestPdfExtractor::damagedFile() {
  const QString path = m_dir.filePath("damaged.pdf");
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("%PDF-1.7\n" + QByteArray(4096, '\x9f') + "\n%%EOF\n");
  f.close();
  const ExtractionResult r = extract(path);
  // Either refused, or opened (MuPDF repairs) with nothing in it: no crash.
  if (r.error.isEmpty())
    QCOMPARE(r.chunks.size(), 0);
}

void TestPdfExtractor::truncatedFile() {
  // A valid PDF cut in the middle of its content: MuPDF repairs what it
  // can, and errors on the pages it cannot read are caught page by page.
  const QString lorem = QString("Some words of a long page. ").repeated(40);
  const QString path = writePdf("full.pdf", {lorem, lorem, lorem});
  QFile full(path);
  QVERIFY(full.open(QIODevice::ReadOnly));
  const QByteArray bytes = full.readAll();
  full.close();
  const QString cut = m_dir.filePath("cut.pdf");
  QFile out(cut);
  QVERIFY(out.open(QIODevice::WriteOnly));
  out.write(bytes.left(bytes.size() * 2 / 3));
  out.close();
  const ExtractionResult r = extract(cut);
  if (r.error.isEmpty())
    QCOMPARE(r.pageMetadata.size(), r.pageCount); // one entry per page
}

QTEST_MAIN(TestPdfExtractor)
#include "tst_pdfextractor.moc"
