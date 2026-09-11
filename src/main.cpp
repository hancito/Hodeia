#include "configmanager.h"
#include "database.h"
#include "enginemanager.h"
#include "helpers.h"
#include "llama.h"
#include "openalex.h"
#include "querycontroller.h"
#include "summarythreadmanager.h"
#include "thememanager.h"

#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <print>

/// Install a global llama.cpp log callback early (before any model loads)
/// so that embedding model init and chat backend share the same filter.
static void initLlamaLogging() {
  llama_log_set(
      [](enum ggml_log_level level, const char *text, void *) {
        if (level >= GGML_LOG_LEVEL_ERROR) {
          std::print("llama.cpp: {}\n", text);
        }
      },
      nullptr);
}

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
  app.setApplicationName("Hodeia");
  app.setApplicationDisplayName("Hodeia");
  app.setWindowIcon(QIcon(":/Resources/hodeia.png"));

  // Force Fusion style to avoid platform-dependent styles (e.g. Breeze on KDE)
  // that conflict with our manual palette theming.
  QQuickStyle::setStyle("Fusion");

  // Suppress llama.cpp verbosity (must run before any model is loaded)
  initLlamaLogging();

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
  EngineController engineController(&configManager,
                                    &databaseManager.vecManager());

  // Create the Summarizer (orchestrates local/remote backends)
  SummaryController summaryController(&configManager, &databaseManager);

  // Create the Query controller (RAG over the paper library)
  QueryController queryController(&configManager, &databaseManager);

  // Create the ModelHelper (manages model discovery for QML)
  ModelHelper modelHelper(&configManager);

  // Create the OpenAlex reference service (independent background thread)
  OpenAlexClient openAlexClient(&configManager, &databaseManager);
  databaseManager.setOpenAlexClient(&openAlexClient);
  QObject::connect(&summaryController, &SummaryController::summaryFinished,
                   &openAlexClient, &OpenAlexClient::checkEmptyReferences);

  // Startup catch-up: fetch references for papers summarized in prior sessions
  QTimer::singleShot(0, &openAlexClient, &OpenAlexClient::checkEmptyReferences);

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("themeManager", &themeManager);
  engine.rootContext()->setContextProperty("configManager", &configManager);
  engine.rootContext()->setContextProperty("databaseManager", &databaseManager);
  engine.rootContext()->setContextProperty("engineController",
                                           &engineController);
  engine.rootContext()->setContextProperty("summaryController",
                                           &summaryController);
  engine.rootContext()->setContextProperty("queryController", &queryController);
  engine.rootContext()->setContextProperty("modelHelper", &modelHelper);
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
  engine.loadFromModule("HodeiaCPP", "Main");

  return QGuiApplication::exec();
}
