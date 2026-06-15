#include "headers/ingestionengine.h"
#include <QFile>
#include <QCryptographicHash>
extern "C" {
#include <mupdf/fitz.h>
}


PdfWorker::PdfWorker(QObject *parent)
    : QObject(parent) {}

PdfWorker::~PdfWorker() = default;

void PdfWorker::processPDF(const QString &filePath,
                           const QString &dbPath) const {}

QString PdfWorker::getFileHash(const QUrl &filePath) const {
    QString localPath = filePath.toLocalFile();
        if (localPath.isEmpty()) {
            return QString("Error: Invalid local file path.");
        }
        QFile file(localPath);
        if (!file.open(QIODevice::ReadOnly)) {
            return QString("Error: Could not open file.");
        }

        // Calculate MD5 hash (you can change this to Sha256 if preferred)
        QCryptographicHash decrypter(QCryptographicHash::Md5);
        if (decrypter.addData(&file)) {
            return QString(decrypter.result().toHex());
        }
        return QString("Error: Hash calculation failed.");
}

QString PdfWorker::extractText(const QUrl &fileUrl)
{
    QString localPath = fileUrl.toLocalFile();
    if (localPath.isEmpty()) return "Error: Invalid path.";

    fz_context *ctx = nullptr;
    fz_document *doc = nullptr;
    QString extractedText = "";

    // 1. Create MuPDF context
    ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) {
        return "Error: Cannot register MuPDF context.";
    }

    // Register default document handlers (PDF, XPS, CBZ, etc.)
    fz_try(ctx) {
        fz_register_document_handlers(ctx);
    }
    fz_catch(ctx) {
        fz_drop_context(ctx);
        return "Error: Failed to register document handlers.";
    }

    // 2. Open the PDF document
    fz_try(ctx) {
        doc = fz_open_document(ctx, localPath.toUtf8().constData());
    }
    fz_catch(ctx) {
        fz_drop_context(ctx);
        return "Error: Cannot open document " + localPath;
    }

    // 3. Count total pages and loop through them
    int pageCount = 0;
    fz_try(ctx) {
        pageCount = fz_count_pages(ctx, doc);
    }
    fz_catch(ctx) {
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
        return "Error: Cannot count pages.";
    }

    for (int i = 0; i < pageCount; ++i) {
        fz_page *page = nullptr;
        fz_stext_page *textPage = nullptr;
        fz_device *dev = nullptr;

        fz_try(ctx) {
            // Load the page
            page = fz_load_page(ctx, doc, i);

            // Create a structure to hold extracted text
            textPage = fz_new_stext_page(ctx, fz_bound_page(ctx, page));

            // Create a text-extracting device that feeds into our textPage structure
            dev = fz_new_stext_device(ctx, textPage, nullptr);

            // Run the page through the device to extract text
            fz_run_page(ctx, page, dev, fz_identity, nullptr);

            // Close the device immediately after use
            fz_close_device(ctx, dev);

            // Fetch the layout text from MuPDF as a string block
            fz_buffer *buf = fz_new_buffer(ctx, 256);
            fz_print_stext_page_as_text(ctx, buf, textPage);

            // Append page content to our return variable
            const char *pageText = fz_string_from_buffer(ctx, buf);
            extractedText.append(QString::fromUtf8(pageText));
            extractedText.append("\n--- Page Break ---\n");

            // Clean up buffers for this specific page loop
            fz_drop_buffer(ctx, buf);
        }
        fz_always(ctx) {
            // Clean up structures safely even if exceptions occurred
            if (dev) fz_drop_device(ctx, dev);
            if (textPage) fz_drop_stext_page(ctx, textPage);
            if (page) fz_drop_page(ctx, page);
        }
        fz_catch(ctx) {
            qWarning() << "Error extracting page " << i;
        }
    }

    // 4. Final Cleanup
    fz_drop_document(ctx, doc);
    fz_drop_context(ctx);

    return extractedText;
}
