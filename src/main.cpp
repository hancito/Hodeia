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
#include <QTranslator>
#include <print>

/// Load the translation catalog for the given language code (e.g. "en", "fr").
/// Returns true if a .qm file was found and loaded; false for "en" or when
/// no catalog exists (the source strings are shown as-is).
static bool loadTranslator(QGuiApplication &app, const QString &lang) {
  static QTranslator s_translator;
  app.removeTranslator(&s_translator);

  if (lang == "en" || lang.isEmpty())
    return true; // English = source language, no catalog needed

  const QString qmFile =
      app.applicationDirPath() + "/i18n/hodeia_" + lang + ".qm";
  if (s_translator.load(qmFile)) {
    app.installTranslator(&s_translator);
    return true;
  }
  qWarning() << "Translator not loaded:" << qmFile;
  return false;
}

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

  // Load the translation catalog for the saved language (before QML engine
  // so that qsTr() calls in QML pick up the right strings on first load).
  loadTranslator(app, configManager.language());

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

  // Create the Helpers (manages model discovery, summary formatting, PDF
  // export)
  Helpers helpers(&configManager);

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
  engine.rootContext()->setContextProperty("helpers", &helpers);
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

  // When the user changes the language in Settings, reload the translator
  // and the QML engine so all qsTr() calls re-evaluate with the new catalog.
  QObject::connect(&configManager, &ConfigManager::languageChanged,
                   [&engine, &app, &configManager]() {
                     loadTranslator(app, configManager.language());
                     engine.clearComponentCache();
                     engine.loadFromModule("HodeiaCPP", "Main");
                   });

  engine.loadFromModule("HodeiaCPP", "Main");

  return QGuiApplication::exec();
}
