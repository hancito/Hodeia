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
#include <QTextStream>
#include <QTimer>
#include <QTranslator>
#include <print>
#include <string>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

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

/// Route qDebug()/qWarning()/qCritical() to an attached console when the app
/// was launched from a terminal.  A WIN32 (GUI-subsystem) build has no console
/// of its own, so normally qDebug() output goes to a null device.  When a
/// parent console exists (i.e. the user ran the exe from a shell), attach to
/// it and mirror Qt's message stream there via WriteConsoleW (bypasses C
/// stdio buffering, so no freopen / filebuf rebinding needed).  Double-clicking
/// the exe (no console) stays silent, so this is safe for both dev and release.
#ifdef Q_OS_WIN
static bool s_consoleAttached = false;
static void consoleWrite(const QString &text) {
  if (!s_consoleAttached)
    return;
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h == INVALID_HANDLE_VALUE)
    return;
  std::wstring wide = text.toStdWString();
  wide.push_back(L'\n');
  DWORD written = 0;
  WriteConsoleW(h, wide.c_str(), (DWORD)wide.size(), &written, nullptr);
}
#endif

static void initConsoleLogging() {
#ifdef Q_OS_WIN
  s_consoleAttached = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
  if (!s_consoleAttached) {
    return; // No parent console — qDebug() goes to null device as before.
  }
#endif
  qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &,
                            const QString &msg) {
    const char *tag = "DEBUG";
    switch (type) {
    case QtDebugMsg:
      tag = "DEBUG";
      break;
    case QtInfoMsg:
      tag = "INFO";
      break;
    case QtWarningMsg:
      tag = "WARN";
      break;
    case QtCriticalMsg:
      tag = "CRIT";
      break;
    case QtFatalMsg:
      tag = "FATAL";
      break;
    }
    const QString line = QString::fromLatin1("[") + QString::fromLatin1(tag) +
                         QString::fromLatin1("] ") + msg;
#ifdef Q_OS_WIN
    if (s_consoleAttached) {
      consoleWrite(line);
    }
#endif
    if (type == QtFatalMsg) {
      std::print("[FATAL] {}\n", msg.toUtf8().constData());
      std::abort();
    }
  });
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

  // Attach to a parent console (if any) and route qDebug() there.
  // Must run before any qDebug() call so early startup messages are captured.
  initConsoleLogging();

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
