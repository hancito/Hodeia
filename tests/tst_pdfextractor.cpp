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

#include <thread>
#include <vector>

namespace {

// A 96x96 JPEG 2000 image (opj_compress), as found in many scanned papers.
const char kJp2Base64[] =
    "AAAADGpQICANCocKAAAAFGZ0eXBqcDIgAAAAAGpwMiAAAAAtanAyaAAAABZpaGRyAAAAYAAAAGAA"
    "Aw8HAAAAAAAPY29scgEAAAAAABAAAAHpanAyY/9P/1EALwAAAAAAYAAAAGAAAAAAAAAAAAAAAGAA"
    "AABgAAAAAAAAAAAAAw8BAQ8BAQ8BAf9SAAwAAAABAQUEBAAB/1wAE0CAiIiQiIiQiIiQiIiQiIiQ"
    "/2QAJQABQ3JlYXRlZCBieSBPcGVuSlBFRyB2ZXJzaW9uIDIuNS40/5AACgAAAAABYgAB/5PP/Cgg"
    "COwrQmAAAAPf+IB4DLi0Jxe3Uvh71PFRhUxH/3iwkATMSUWx+JYSig6B3kZKpJBfD4Ch/0GCQAy7"
    "rugDdKyJf6H/QIJADj1cdAG6VIt/gKD/YAHANknEOCuCGYKnjkfkUmOg/2ABgDehyDgrghmCp7bx"
    "v4Cgf4GghCAqjtGgcAAA/omFEI7K7FqptVpkUw/V0j+gf4FQhAqqTtGgcAAA/omFEC5YGQ8A1Uqs"
    "gKA/kgDKnEqiM/k4kgAAYQfbiEs1U723gGEQM+HSd4pabIr/f6A/kZDKluqSM/k4kgAAYQfbiEvG"
    "FC8Q6WNpU1dngKAfsXDxfQCqLIPuI4gAAYSEBMbCQl6t5AAwkJdycwAYSDuDWEg6J++ZOmzQn/Q4"
    "sq6foB+xYPF8qqksg+4jiAABhIQExsJCXq3kADCQl3JzABhIO4NYSDqUV1qKrXIOxh/x/9k=";

/// A PDF whose pages each draw their own JPEG 2000 image (/JPXDecode) under
/// a line of text (long enough to skip the OCR fallback).
QByteArray jpxPdf(int pages) {
  const QByteArray jp2 = QByteArray::fromBase64(kJp2Base64);
  QList<QByteArray> objects; // object n is objects[n - 1]
  objects << "<< /Type /Catalog /Pages 2 0 R >>" << QByteArray()
          << "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";
  QByteArray kids;
  for (int i = 0; i < pages; ++i) {
    const int image = objects.size() + 1;
    objects << "<< /Type /XObject /Subtype /Image /Width 96 /Height 96 "
               "/Filter /JPXDecode /Length " +
                   QByteArray::number(jp2.size()) + " >>\nstream\n" + jp2 +
                   "\nendstream";
    // Short lines: text past the page edge is clipped away.
    const QByteArray content =
        "q 200 0 0 200 50 40 cm /Im0 Do Q BT /F1 7 Tf 9 TL 10 285 Td (Page " +
        QByteArray::number(i + 1) + " of a paper) Tj" +
        QByteArray(" T* (with a JPEG 2000 figure below it.) Tj").repeated(4) +
        " ET";
    objects << "<< /Length " + QByteArray::number(content.size()) +
                   " >>\nstream\n" + content + "\nendstream";
    const int page = objects.size() + 1;
    objects << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] "
               "/Resources << /Font << /F1 3 0 R >> /XObject << /Im0 " +
                   QByteArray::number(image) + " 0 R >> >> /Contents " +
                   QByteArray::number(image + 1) + " 0 R >>";
    kids += QByteArray::number(page) + " 0 R ";
  }
  objects[1] = "<< /Type /Pages /Kids [" + kids + "] /Count " +
               QByteArray::number(pages) + " >>";

  QByteArray pdf = "%PDF-1.5\n";
  QList<qsizetype> offsets;
  for (int n = 0; n < objects.size(); ++n) {
    offsets << pdf.size();
    pdf += QByteArray::number(n + 1) + " 0 obj\n" + objects[n] + "\nendobj\n";
  }
  const qsizetype xref = pdf.size();
  pdf += "xref\n0 " + QByteArray::number(objects.size() + 1) +
         "\n0000000000 65535 f \n";
  for (qsizetype off : offsets)
    pdf += QByteArray::number(off).rightJustified(10, '0') + " 00000 n \n";
  pdf += "trailer\n<< /Size " + QByteArray::number(objects.size() + 1) +
         " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xref) +
         "\n%%EOF\n";
  return pdf;
}

} // namespace

class TestPdfExtractor : public QObject {
  Q_OBJECT

private slots:
  void textPages();
  void pageWithoutText();
  void damagedFile();
  void truncatedFile();
  void parallelJpxImages();

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

// Several files with JPEG 2000 images read at once, as the import's
// extraction threads do.  MuPDF's JPX decoder keeps its context in a global
// guarded by a lock every context must share: without it, one thread's
// context was reset under another (crash in fz_free).
void TestPdfExtractor::parallelJpxImages() {
  constexpr int kFiles = 6, kPages = 12;
  QStringList paths;
  for (int f = 0; f < kFiles; ++f) {
    const QString path = m_dir.filePath(QString("jpx%1.pdf").arg(f));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    // A different trailing comment per file: a different hash, so the
    // threads do not write the same page images.
    file.write(jpxPdf(kPages) + "% file " + QByteArray::number(f) + "\n");
    file.close();
    paths << path;
  }
  std::vector<ExtractionResult> results(kFiles);
  std::vector<std::thread> threads;
  for (int f = 0; f < kFiles; ++f)
    threads.emplace_back(
        [this, &results, &paths, f] { results[f] = extract(paths[f]); });
  for (std::thread &t : threads)
    t.join();
  for (const ExtractionResult &r : results) {
    QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
    QCOMPARE(r.pageCount, kPages);
    QVERIFY(r.pageMetadata[0].hasFigure); // the image was decoded
    QVERIFY(!r.pageMetadata[0].isScanned);
  }
}

QTEST_MAIN(TestPdfExtractor)
#include "tst_pdfextractor.moc"
