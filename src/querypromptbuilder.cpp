#include "querypromptbuilder.h"

#include "metadataresolver.h"
#include "prompts.h"

#include <QDebug>
#include <QImage>
#include <QStringList>


namespace QueryPrompt {

QString fixedText(const QString &question, const QString &language) {
  return Prompts::querySystem(language) +
         Prompts::queryUserMessage(question, language);
}

PromptPayload build(const QList<PageContext> &pages, const QString &question,
                    bool useVisual, const QString &language) {
  PromptPayload payload;
  payload.systemPrompt = Prompts::querySystem(language);

  // Build structured text context: one block per page.
  QStringList blocks;
  for (int i = 0; i < pages.size(); ++i) {
    const PageContext &pc = pages[i];
    const QString src = MetadataResolver::paperLabel(pc).sourceForm();

    // The bracketed number is the reference tag the model cites with; it is
    // resolved back to this page by CitationFormat::resolveReferenceTags().
    QString block = Prompts::queryPageHeader(i + 1, src, pc.displayPage());

    for (const QString &chunk : pc.textChunks)
      block += chunk + "\n[---]\n";

    block += "\n";
    blocks.append(block);

    // Build PageBlock with image
    PageBlock pb;
    pb.pageNumber = i + 1;
    pb.text = block;

    if (useVisual && pc.withImage) {
      QImage img(pc.imageFile);
      if (!img.isNull()) {
        pb.visualContext = PageImage::forModel(img);
        // Legacy (non-interleaved) multimodal path: reuse the decoded pixels
        // instead of decoding the PNG a second time.
        payload.visualContext.push_back(pb.visualContext);
      }
    }
    payload.pageBlocks.push_back(std::move(pb));
  }

  payload.textContext = blocks.join("\n");
  payload.userMessage = Prompts::queryUserMessage(question, language);

  if (useVisual)
    qDebug() << "[QueryPrompt] Visual context —" << payload.visualContext.size()
             << "image(s) loaded";

  return payload;
}

} // namespace QueryPrompt
