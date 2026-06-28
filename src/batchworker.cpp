#include "headers/batchworker.h"
#include "headers/database.h"

#include <QDebug>

BatchWorker::BatchWorker(DatabaseManager *database, QObject *parent)
    : QObject(parent), m_database(database) {}

QString BatchWorker::retrieveContext(const QString &paperHash) const {
  if (!m_database || !m_database->isOpen()) {
    qWarning() << "[BatchWorker] database not open";
    return QString();
  }

  auto &db = m_database->db();
  auto &vecMgr = m_database->vecManager();

  QList<ChunkSearchResult> chunks = vecMgr.getChunksByHash(db, paperHash);
  if (chunks.isEmpty()) {
    qDebug() << "[BatchWorker] no chunks for hash:" << paperHash;
    return QString();
  }

  QString context;
  for (const auto &c : chunks) {
    context += QString("Page %1: %2\n\n").arg(c.page).arg(c.text);
  }

  qDebug() << "[BatchWorker] retrieved" << chunks.size()
           << "chunks, length:" << context.length() << "for hash:" << paperHash;
  qDebug() << "[BatchWorker] retrieved" << context;
  return context;
}
