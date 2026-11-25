#include "querypromptbuilder.h"

#include "metadataresolver.h"
#include "prompts.h"

#include <QDebug>
#include <QImage>


namespace QueryPrompt {

QString fixedText(const QString &question, const QString &language) {
  return Prompts::querySystem(language) +
         Prompts::queryUserMessage(question, language);
}

PromptPayload build(const QList<PageContext> &pages, const QString &question,
                    bool useVisual, const QString &language) {
  PromptPayload payload;
  payload.systemPrompt = Prompts::querySystem(language);

  // One block per page.
  int images = 0;
  for (int i = 0; i < pages.size(); ++i) {
    const PageContext &pc = pages[i];
    const QString src = MetadataResolver::paperLabel(pc).sourceForm();

    // The bracketed number is the reference tag the model cites with; it is
    // resolved back to this page by CitationFormat::resolveReferenceTags().
    QString block = Prompts::queryPageHeader(i + 1, src, pc.displayPage());

    for (const QString &chunk : pc.textChunks)
      block += chunk + "\n[---]\n";

    block += "\n";

    // Build PageBlock with image
    PageBlock pb;
    pb.pageNumber = i + 1;
    pb.text = block;

    if (useVisual && pc.withImage) {
      QImage img(pc.imageFile);
      if (!img.isNull()) {
        pb.visualContext = PageImage::forModel(img);
        ++images;
      }
    }
    payload.pageBlocks.push_back(std::move(pb));
  }

  payload.userMessage = Prompts::queryUserMessage(question, language);

  if (useVisual)
    qDebug() << "[QueryPrompt] Visual context —" << images
             << "image(s) loaded";

  return payload;
}

} // namespace QueryPrompt
