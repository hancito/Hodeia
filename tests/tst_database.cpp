// DatabaseManager: the shared connection is never closed under a worker
// operation, nor a project deleted or cleared under an import.

#include "configmanager.h"
#include "database.h"
#include "sqlitevec.h"

#include <QDir>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QThread>

class TestDatabase : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void init();
  void cleanup();

  void clearReleasesBackgroundConnections();
  void operationBlocksProjectChanges();
  void operationWaitsForSwitch();
  void importBlocksDeleteAndClear();

private:
  ConfigManager *m_config = nullptr;
  SqliteVecManager *m_embeddings = nullptr;
  DatabaseManager *m_database = nullptr;
};

void TestDatabase::initTestCase() {
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setApplicationName("HodeiaTestDatabase");
}

void TestDatabase::init() {
  QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
      .removeRecursively();
  m_config = new ConfigManager;
  m_embeddings = new SqliteVecManager(m_config);
  m_database = new DatabaseManager(m_config, *m_embeddings);
  QVERIFY(m_database->isOpen());
}

void TestDatabase::cleanup() {
  delete m_database;
  delete m_embeddings;
  delete m_config;
  QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
      .removeRecursively();
}

void TestDatabase::clearReleasesBackgroundConnections() {
  // The reindexer listens to databaseClosing to close its own connection
  // before the project's files are deleted.
  QSignalSpy closing(m_database, &DatabaseManager::databaseClosing);
  QCOMPARE(m_database->clearActiveProject(), QString());
  QCOMPARE(closing.count(), 1);
  QVERIFY(m_database->isOpen());
}

void TestDatabase::operationBlocksProjectChanges() {
  const QString first = m_config->lastProject();
  m_database->beginDbOperation();
  QVERIFY(!m_database->createProject("Second").isEmpty());
  QCOMPARE(m_config->lastProject(), first);
  QVERIFY(!m_database->clearActiveProject().isEmpty());
  QVERIFY(!m_database->deleteProjects({first}).isEmpty());
  QVERIFY(m_database->isOpen());
  m_database->endDbOperation();

  QCOMPARE(m_database->createProject("Second"), QString());
  QCOMPARE(m_config->lastProject(), QString("Second"));
}

void TestDatabase::operationWaitsForSwitch() {
  // An operation that begins while the GUI thread holds the connection
  // (closing/reopening it) waits, then finds it open.
  const QString first = m_config->lastProject();
  QCOMPARE(m_database->createProject("Second"), QString());
  bool openWhenStarted = false;
  QThread *worker = QThread::create([this, &openWhenStarted] {
    m_database->beginDbOperation();
    openWhenStarted = m_database->isOpen();
    m_database->endDbOperation();
  });
  QSignalSpy switched(m_database, &DatabaseManager::databaseSwitched);
  connect(m_database, &DatabaseManager::databaseSwitched, worker,
          [worker] { worker->start(); }, Qt::DirectConnection);
  QCOMPARE(m_database->switchProject(first), QString());
  QVERIFY(worker->wait(5000));
  QCOMPARE(switched.count(), 1);
  QVERIFY(openWhenStarted);
  delete worker;
}

void TestDatabase::importBlocksDeleteAndClear() {
  const QString first = m_config->lastProject();
  QCOMPARE(m_database->createProject("Second"), QString());
  // An import still writing to the first project after a switch.
  m_database->setIngestionTarget(m_config->projectDbPath(first));
  QVERIFY(!m_database->deleteProjects({first}).isEmpty());
  QVERIFY(m_config->projectList().contains(first));
  QCOMPARE(m_database->switchProject(first), QString()); // switching is safe
  QVERIFY(!m_database->clearActiveProject().isEmpty());

  m_database->setIngestionTarget(QString());
  QCOMPARE(m_database->clearActiveProject(), QString());
  QCOMPARE(m_database->deleteProjects({"Second"}), QString());
  QVERIFY(!m_config->projectList().contains("Second"));
}

QTEST_MAIN(TestDatabase)
#include "tst_database.moc"
