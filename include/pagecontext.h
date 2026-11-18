#pragma once

#include "zoterosource.h"

#include <QString>
#include <QStringList>
#include <optional>

/// One page worth of context aggregated from vector search results
/// (Retriever), turned into a prompt block (QueryPrompt) and cited in the
/// answer (CitationFormat).
struct PageContext {
  QString paperHash;
  int page = 0;            ///< 0-based, as stored in the database
  QString cleanTitle;      ///< chunks.clean_title (summary title or file name)
  QString citation;        ///< chunks.citation ("Authors (year)" once summarized)
  QStringList textChunks;  ///< may hold multiple chunks from the same page
  QString imageFile;       ///< absolute PNG path, empty if unavailable
  bool withImage = false;  ///< image sent to the model (visual mode, within
                           ///< the image cap and the token budget)
  /// Zotero's metadata of the paper, set only when they take precedence
  /// (zoteroPriority): see MetadataResolver::attachZotero().
  std::optional<ZoteroMeta> zotero;

  /// 1-based page number, as readers and the page image files
  /// (hash_page_001.png) count.
  int displayPage() const { return page + 1; }
};
