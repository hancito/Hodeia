#include "retriever.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMap>
#include <algorithm>

Retriever::Retriever(SqliteDb &db, SqliteVecManager &embeddings)
    : m_db(db), m_embeddings(embeddings) {}

int Retriever::countTokens(const QString &text) const {
  const int n = m_embeddings.countTokens(text);
  return n >= 0 ? n : int(text.length() / 3) + 1;
}

int Retriever::candidateCount(int budgetTokens) {
  return qBound(80, budgetTokens / 140, 2000);
}

QList<PageContext> Retriever::retrieve(const QString &query,
                                       const Options &options) const {
  const int k = candidateCount(options.budgetTokens);
  const QList<ChunkSearchResult> chunks = m_embeddings.search(m_db, query, k, options.papers);
  if (chunks.isEmpty()) {
    qDebug() << "[Retriever] no chunks found for query";
    return {};
  }
  qDebug() << "[Retriever] retrieved" << chunks.size() << "chunks (k =" << k
           << ")";
  return groupByPage(chunks, options,
                     [this](const QString &t) { return countTokens(t); });
}

QList<PageContext> Retriever::groupByPage(
    const QList<ChunkSearchResult> &chunks, const Options &options,
    const std::function<int(const QString &)> &countTokens) {
  if (chunks.isEmpty())
    return {};
  const int budgetTokens = options.budgetTokens;

  // Sorted nearest first.
  const float best = chunks.first().distance;
  const float cutoff = best + RelevanceMargin;

  // ── Fill the budget by relevance, grouping chunks by (paper, page) ──
  QMap<QString, int> pageKeyToIndex; // "hash|page" → index in `pages`
  QList<PageContext> pages;

  const QDir imgDir(options.imageDir);
  const bool hasImgDir = !options.imageDir.isEmpty() && imgDir.exists();

  int used = 0;
  int images = 0;
  int kept = 0;
  int skippedBudget = 0;
  int cutByRelevance = 0;
  QList<float> keptDistances;

  for (qsizetype i = 0; i < chunks.size(); ++i) {
    const ChunkSearchResult &c = chunks[i];
    if (c.distance > cutoff) {
      // Sorted by distance: everything after is farther still.
      cutByRelevance = int(chunks.size() - i);
      break;
    }
    if (budgetTokens - used < 64)
      break; // budget full

    const int chunkCost = countTokens(c.text) + ChunkSeparatorTokens;
    const QString key = c.paperHash + "|" + QString::number(c.page);

    auto it = pageKeyToIndex.find(key);
    if (it != pageKeyToIndex.end()) {
      // Existing page — add the chunk if it is new and fits.
      PageContext &pc = pages[it.value()];
      if (pc.textChunks.contains(c.text))
        continue;
      if (used + chunkCost > budgetTokens) {
        ++skippedBudget;
        continue; // a smaller, less relevant chunk may still fit
      }
      pc.textChunks.append(c.text);
      used += chunkCost;
      ++kept;
      keptDistances << c.distance;
      continue;
    }

    // New page: header + chunk (+ image in visual mode, while it fits).
    PageContext pc;
    pc.paperHash = c.paperHash;
    pc.page = c.page;
    pc.cleanTitle = c.cleanTitle;
    pc.citation = c.citation;
    pc.textChunks.append(c.text);

    int cost = PageHeaderTokens + chunkCost;
    if (used + cost > budgetTokens) {
      ++skippedBudget;
      continue;
    }

    // Resolve image path: hash_page_NNN.png
    if (hasImgDir) {
      const QString imgFile = QString("%1_page_%2.png")
                                  .arg(c.paperHash)
                                  .arg(pc.displayPage(), 3, 10, QChar('0'));
      const QString absoluteImg = imgDir.absoluteFilePath(imgFile);
      if (QFile::exists(absoluteImg))
        pc.imageFile = absoluteImg;
    }
    if (options.useVisual && !pc.imageFile.isEmpty() && images < MaxImages &&
        used + cost + ImageTokens <= budgetTokens) {
      pc.withImage = true;
      cost += ImageTokens;
      ++images;
    }

    pageKeyToIndex.insert(key, int(pages.size()));
    pages.append(pc);
    used += cost;
    ++kept;
    keptDistances << c.distance;
  }

  std::sort(keptDistances.begin(), keptDistances.end());
  qDebug() << "[Retriever] kept" << kept << "chunks in" << pages.size()
           << "pages," << images << "image(s) |" << used << "/"
           << budgetTokens << "tokens |" << cutByRelevance
           << "below relevance," << skippedBudget << "over budget";
  if (!keptDistances.isEmpty())
    qDebug() << "[Retriever] distances: best" << best << "| median"
             << keptDistances[keptDistances.size() / 2] << "| worst kept"
             << keptDistances.last() << "| cutoff" << cutoff;
  return pages;
}
