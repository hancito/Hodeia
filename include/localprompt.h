#pragma once

#include <QString>
#include <cstdint>
#include <vector>

class QImage;
struct common_chat_templates;
struct common_chat_prompt;

/// One scanned page as raw RGB pixels (no base64 encoding).
/// Used to pass image data from the UI layer to the inference engine without
/// allocating intermediate Qt image objects on the C API boundary.
struct PageImage {
  int width;
  int height;
  std::vector<unsigned char> data; ///< RGB888, width*height*3 bytes

  /// Build a PageImage by COPYING the pixels out of a QImage so the result
  /// is self-contained and does not alias the (possibly temporary) source
  /// image's buffer.
  static PageImage fromQImage(const QImage &img);

  /// Largest page image sent to a vision model (~120 DPI for a letter/A4
  /// page). Image tokens grow with pixels (measured on a 150 DPI page:
  /// Qwen3.5 ≈ 1 token / 1000 px, Gemma 4 ≈ 0.43), so the cap keeps the
  /// prompt independent of the resolution the page PNGs are stored at.
  static constexpr int kModelMaxPixels = 900'000;

  /// fromQImage() after scaling @p img down (never up, aspect ratio kept) to
  /// at most kModelMaxPixels.
  static PageImage forModel(const QImage &img);
};

/// One page: OCR text paired with an optional rendered page image.
/// Used to build interleaved prompts: <--image--> <--text--> per page.
struct PageBlock {
  int pageNumber;          ///< 1-based page number
  QString text;            ///< OCR text for this page
  PageImage visualContext; ///< page image (empty data = no image)
};

/// All inputs needed to produce a summarization response.
/// The backend assembles these fields into a formatted chat prompt, applies
/// the model's chat template, and feeds it to the inference engine.
struct PromptPayload {
  QString systemPrompt;
  /// System prompt to use instead of systemPrompt when the backend cannot
  /// pass the images (no projector, or it failed to load), so the model is
  /// not told about figures it never receives.  Empty = use systemPrompt.
  QString textOnlySystemPrompt;
  QString userMessage;
  QString textContext;
  std::vector<PageImage> visualContext; ///< raw RGB page images
  std::vector<PageBlock> pageBlocks;    ///< interleaved page-by-page (takes
                                        ///< precedence when non-empty)
};

/// Prompt assembly for the local backend, shared by the token count of the
/// memory probe (VramFit::probeModel) and the real inference prompt
/// (LocalBackend), so the two can never diverge.
namespace LocalPrompt {

/// Images a payload carries, in prompt order (one per media marker).
std::vector<PageImage> collectImages(const PromptPayload &payload);

/// System prompt matching what the model will actually receive.
const QString &systemPrompt(const PromptPayload &payload, bool withImages);

/// Raw user-prompt text (no chat template, no image embeddings).  `marker`
/// is the mtmd media marker, empty for text-only inference; `maxChars` caps
/// the prompt (0: no cap, used for token counting).
QString buildUserPrompt(const PromptPayload &payload, const QString &marker,
                        int32_t maxChars);

/// Wrap a system + user message in the model's chat template, thinking
/// disabled.  May throw on a malformed Jinja template.
common_chat_prompt applyChatTemplate(const common_chat_templates *tmpls,
                                     const QString &systemMessage,
                                     const QString &userMessage);

} // namespace LocalPrompt
