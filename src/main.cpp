#include "headers/configmanager.h"
#include "headers/database.h"
#include "headers/enginemanager.h"
#include "headers/summarizer.h"
#include "headers/thememanager.h"
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>

// Register the custom Fonts
static void registerApplicationFonts(const QString &fontsDir) {
  QDir dir(fontsDir);
  if (!dir.exists()) {
    qWarning() << "Fonts directory not found:" << fontsDir;
    return;
  }

  const QStringList fontSubdirs =
      dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString &subdir : fontSubdirs) {
    QDir subDir(dir.absolutePath() + "/" + subdir);
    const QStringList ttfFiles =
        subDir.entryList(QStringList() << "*.ttf", QDir::Files);
    for (const QString &file : ttfFiles) {
      const QString fullPath = subDir.absoluteFilePath(file);
      const int id = QFontDatabase::addApplicationFont(fullPath);
      if (id == -1) {
        qWarning() << "Failed to load font:" << fullPath;
      } else {
        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        for (const QString &family : families) {
          qDebug() << "Registered font family:" << family;
        }
      }
    }
  }
}

// Main app section. The entry point
int main(int argc, char *argv[]) {
  QGuiApplication app(argc, argv);
  app.setWindowIcon(QIcon(":/Resources/paperize.png"));

  // Register custom fonts
  registerApplicationFonts(app.applicationDirPath() + "/Fonts");

  // Create ConfigManager
  ConfigManager configManager;

  // Create ThemeManager and adjust path to the JSON themes folder (relative
  // to the executable)
  ThemeManager themeManager;
  themeManager.loadThemesFromFolder(app.applicationDirPath() + "/ui/themes");

  // Create DatabaseManager with config reference for project DB resolution
  DatabaseManager databaseManager(&configManager);

  // Create the PDF ingestion worker
  EngineController engineController;

  // Create the Summarizer (orchestrates local/remote backends)
  Summarizer summarizer(&configManager, &databaseManager);

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("themeManager", &themeManager);
  engine.rootContext()->setContextProperty("configManager", &configManager);
  engine.rootContext()->setContextProperty("databaseManager", &databaseManager);
  engine.rootContext()->setContextProperty("engineController",
                                           &engineController);
  engine.rootContext()->setContextProperty("summarizer", &summarizer);
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
  engine.loadFromModule("Paperize_CPP", "Main");

  return QGuiApplication::exec();
}
