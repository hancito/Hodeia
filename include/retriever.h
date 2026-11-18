#pragma once

#include "pagecontext.h"
#include "sqlitevec.h"

#include <QList>
#include <QString>
#include <functional>

class SqliteDb;

/// Semantic search over the library: the chunks nearest to a question,
/// grouped by page and cut to a token budget.
class Retriever {
public:
  struct Options {
    int budgetTokens = 0; ///< tokens available for the page contexts
    bool useVisual = false; ///< attach page images (within MaxImages)
    QString imageDir; ///< folder of the page PNGs (hash_page_NNN.png)
  };

  // Chunks farther than (best distance + margin) from the query are dropped,
  // even if the budget has room: they would only add noise.  L2 distance on
  // L2-normalised E5 embeddings (0 = identical, √2 ≈ 1.41 = orthogonal).
  // E5 similarities are compressed into a narrow band, hence a margin
  // relative to the best hit rather than an absolute threshold.
  static constexpr float RelevanceMargin = 0.15f;
  // Visual mode: tokens charged per page image (matches the local backend's
  // per-image estimate) and the most images sent — encoding is slow.
  static constexpr int ImageTokens = 800;
  static constexpr int MaxImages = 12;
  // Prompt tokens per page block header ("=== PAGE CONTEXT [n] — …") and
  // per chunk separator, so the budget isn't overrun by the scaffolding.
  static constexpr int PageHeaderTokens = 32;
  static constexpr int ChunkSeparatorTokens = 4;

  Retriever(SqliteDb &db, SqliteVecManager &embeddings);

  /// Fill `options.budgetTokens` with the chunks most relevant to `query`,
  /// grouped by page, most relevant first.  Empty if nothing was found.
  QList<PageContext> retrieve(const QString &query,
                              const Options &options) const;

  /// Token count with the embedding tokenizer (chars/3 if unavailable).
  int countTokens(const QString &text) const;

  /// Candidate chunks to fetch for a budget: enough to fill it (chunks
  /// average ~350 tokens) with room for the ones the relevance cut and the
  /// dedup will drop.
  static int candidateCount(int budgetTokens);

  /// The grouping and budget filling of retrieve(), on `chunks` sorted
  /// nearest first.
  static QList<PageContext>
  groupByPage(const QList<ChunkSearchResult> &chunks, const Options &options,
              const std::function<int(const QString &)> &countTokens);

private:
  SqliteDb &m_db;
  SqliteVecManager &m_embeddings;
};
