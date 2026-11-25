#include "localprompt.h"
#include "prompts.h"

#include "common/chat.h"

#include <QImage>
#include <cmath>
#include <cstring>

namespace LocalPrompt {

/// Images a payload carries, in prompt order (one per marker).
std::vector<PageImage> collectImages(const PromptPayload &payload) {
  std::vector<PageImage> images;
  for (const auto &pb : payload.pageBlocks)
    if (!pb.visualContext.data.empty())
      images.push_back(pb.visualContext);
  return images;
}

/// System prompt matching what the model will actually receive.
const QString &systemPrompt(const PromptPayload &payload, bool withImages) {
  return withImages || payload.textOnlySystemPrompt.isEmpty()
             ? payload.systemPrompt
             : payload.textOnlySystemPrompt;
}

/// Assemble the raw user-prompt text from a payload (no chat template, no
/// image embeddings).  This is the single source of truth shared by prompt
/// token-counting and the actual inference prompt, so the media marker and
/// structure can never diverge between the two.
///
/// \param marker    media-placeholder string (the mtmd marker).  Empty for
///                  text-only inference: no marker and no image section is
///                  emitted, so the model never sees a literal placeholder.
/// \param maxChars  hard cap on prompt characters.  0 means "no cap" (used
///                  for token counting).  When set, the document body is
///                  truncated so the counted prompt matches what gets fed to
///                  inference.
QString buildUserPrompt(const PromptPayload &payload, const QString &marker,
                        int32_t maxChars) {
  // Reserve room for the userMessage header + truncation suffix so the
  // returned string never exceeds maxChars when a cap is applied.
  int32_t budget = 0;
  if (maxChars > 0) {
    budget = maxChars - static_cast<int32_t>(payload.userMessage.length()) -
             40; // "## DOCUMENT" header + "\n\n... [truncated]" suffix
    if (budget < 256)
      budget = 256;
  }

  QString userPrompt =
      payload.userMessage + "\n\n" + Prompts::documentHeading() + "\n\n";

  // Page by page: its image (marker) before its text.
  for (const auto &pb : payload.pageBlocks) {
    userPrompt += Prompts::pageHeading(pb.pageNumber) + "\n";
    if (!marker.isEmpty() && !pb.visualContext.data.empty())
      userPrompt += marker + "\n";
    userPrompt += pb.text + "\n\n";
    if (budget > 0 && userPrompt.length() > budget) {
      userPrompt.truncate(budget);
      // Never leave a partial media marker ("<__me") at the cut: it would
      // reach the model as literal text.
      for (qsizetype k = marker.length() - 1; k > 0; --k) {
        if (userPrompt.endsWith(QStringView(marker).left(k))) {
          userPrompt.chop(k);
          break;
        }
      }
      userPrompt += "\n\n" + Prompts::truncatedMark();
      break;
    }
  }
  return userPrompt;
}

/// Wrap a system + user message in the model's chat template, with thinking
/// disabled — prevents <|channel>thought tags from reasoning models like
/// Gemma4 which break JSON output extraction. Shared by token counting and
/// inference so both see the same prompt. The result keeps template parts
/// and user-input parts apart (see InferenceEngine::tokenizePrompt). May
/// throw on a malformed Jinja template.
common_chat_prompt applyChatTemplate(const common_chat_templates *tmpls,
                                     const QString &systemMessage,
                                     const QString &userMessage) {
  common_chat_templates_inputs inputs;
  inputs.use_jinja = true;
  inputs.add_generation_prompt = true;
  inputs.enable_thinking = false;

  if (!systemMessage.isEmpty()) {
    common_chat_msg sysMsg;
    sysMsg.role = "system";
    sysMsg.content = systemMessage.toStdString();
    inputs.messages.push_back(std::move(sysMsg));
  }

  common_chat_msg usrMsg;
  usrMsg.role = "user";
  usrMsg.content = userMessage.toStdString();
  inputs.messages.push_back(std::move(usrMsg));

  return common_chat_templates_apply(tmpls, inputs).prompt;
}

} // namespace LocalPrompt

/* ── PageImage ───────────────────────────────────────────────── */

PageImage PageImage::forModel(const QImage &img) {
  const qint64 pixels = static_cast<qint64>(img.width()) * img.height();
  if (pixels <= kModelMaxPixels)
    return fromQImage(img);
  const double scale = std::sqrt(static_cast<double>(kModelMaxPixels) /
                                 static_cast<double>(pixels));
  return fromQImage(img.scaled(static_cast<int>(img.width() * scale),
                               static_cast<int>(img.height() * scale),
                               Qt::IgnoreAspectRatio,
                               Qt::SmoothTransformation));
}

PageImage PageImage::fromQImage(const QImage &img) {
  PageImage pi;
  const QImage rgb = img.format() == QImage::Format_RGB888
                         ? img
                         : img.convertToFormat(QImage::Format_RGB888);
  pi.width = rgb.width();
  pi.height = rgb.height();
  // Copy row by row: QImage pads every scanline to a 4-byte boundary, so for
  // RGB888 with width*3 not a multiple of 4 the raw buffer contains padding
  // bytes that would shear the image. mtmd expects tightly packed
  // width*height*3 bytes.
  const size_t rowBytes = static_cast<size_t>(pi.width) * 3;
  pi.data.resize(rowBytes * static_cast<size_t>(pi.height));
  for (int y = 0; y < pi.height; ++y)
    std::memcpy(pi.data.data() + rowBytes * y, rgb.constScanLine(y), rowBytes);
  return pi;
}
