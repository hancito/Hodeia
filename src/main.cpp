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

#include <cstdio>

// ── Crash / diagnostics log ────────────────────────────────────────────────
// In a GUI-subsystem build (WIN32_EXECUTABLE) there is no console, so qDebug()
// and llama.cpp output go to a null device and are invisible unless launched
// from a terminal.  To diagnose hard crashes we mirror ALL app + llama output
// to a log file (Hodeia.log) in the executable directory, and on Windows we
// install an unhandled-exception filter so a hard crash (access violation /
// C-level abort in ggml/llama) is captured with its exception code before the
// process dies.  The file is overwritten each run and is always inspectable.
static FILE *s_logFile = nullptr;
static QString s_logPath;

static void crashLogWrite(const QString &line) {
  if (!s_logFile)
    return;
  fprintf(s_logFile, "%s\n", line.toUtf8().constData());
  fflush(s_logFile);
}

static void initCrashLog() {
  const QString appDir = QCoreApplication::applicationDirPath();
  s_logPath = appDir + "/Hodeia.log";
  s_logFile = fopen(s_logPath.toUtf8().constData(), "wb");
  // Every log line is flushed (see crashLogWrite), so the file always holds
  // the full output up to the moment of a hard crash.  That is enough to see
  // the last thing the app / llama.cpp did before it died.
}

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
static bool s_consoleAttached = false;
static void consoleWrite(const QString &text) {
#ifdef Q_OS_WIN
  if (!s_consoleAttached)
    return;
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h == INVALID_HANDLE_VALUE)
    return;
  std::wstring wide = text.toStdWString();
  wide.push_back(L'\n');
  DWORD written = 0;
  WriteConsoleW(h, wide.c_str(), (DWORD)wide.size(), &written, nullptr);
#else
  (void)text;
#endif
}

/// Mirror a tagged line to both the attached console (if any) and the crash
/// log file.  Used by the Qt and llama log sinks so diagnostics are captured
/// whether or not the app was launched from a terminal.
static void logOut(const QString &tag, const QString &msg) {
  const QString line =
      QString::fromLatin1("[") + tag + QString::fromLatin1("] ") + msg;
  crashLogWrite(line);
#ifdef Q_OS_WIN
  // Windows: a WIN32 build has no console of its own; mirror to the attached
  // parent console (if any) via WriteConsoleW.  No stderr here (a GUI build's
  // stderr is a null device unless a console is attached).
  if (s_consoleAttached)
    consoleWrite(line);
#else
  // Linux/macOS: keep the default terminal behaviour — the previous
  // (pre-handler) Qt sink printed every qDebug/qWarning to stderr.  That
  // default is now replaced by this handler, so we must re-emit it or the
  // terminal goes silent (only Hodeia.log gets the output).  Writing the
  // tagged line directly mirrors the standard Qt format.
  fprintf(stderr, "%s\n", line.toUtf8().constData());
#endif
}

static void initConsoleLogging() {
#ifdef Q_OS_WIN
  s_consoleAttached = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
  if (!s_consoleAttached) {
    return; // No parent console — qDebug() goes to null device as before.
  }
#endif
  qInstallMessageHandler(
      [](QtMsgType type, const QMessageLogContext &, const QString &msg) {
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
        logOut(tag, msg);
        if (type == QtFatalMsg) {
          std::abort();
        }
      });
}

/// Install a global llama.cpp log callback early (before any model loads)
/// so that embedding model init and chat backend share the same filter.
/// This callback is also the sink for ggml's own logging (ggml_log_set is
/// forwarded by llama_log_set), which is how we surface ggml backend-load
/// diagnostics such as "failed to load ggml-cuda.dll" or "backend ... is not
/// supported on this system" (the latter is logged at INFO level — silently
/// dropped if we only forward ERROR and above).
static void initLlamaLogging() {
  llama_log_set(
      [](enum ggml_log_level level, const char *text, void *) {
        // Surface backend-load failures and warnings without the model
        // loader's INFO metadata dump. GGML_LOG_LEVEL_CONT continues a
        // previous message, so keep it for multi-line output.
        if (level < GGML_LOG_LEVEL_WARN && level != GGML_LOG_LEVEL_CONT)
          return;
        const char *tag = "INFO";
        switch (level) {
        case GGML_LOG_LEVEL_ERROR:
          tag = "CRIT";
          break;
        case GGML_LOG_LEVEL_WARN:
          tag = "WARN";
          break;
        case GGML_LOG_LEVEL_INFO:
          tag = "INFO";
          break;
        default:
          tag = "DEBUG";
          break;
        }
        logOut(tag, QString::fromLatin1("llama.cpp: ") +
                        QString::fromUtf8(text ? text : ""));
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

  // Open the crash/diagnostics log first (before any logging) so every
  // startup line — and any hard crash — is captured in Hodeia.log even when
  // the app is double-clicked with no console.
  initCrashLog();

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

  // When the user changes the language in Settings, reload the translator and
  // then retranslate the already-loaded QML with engine.retranslate()
  QObject::connect(&configManager, &ConfigManager::languageChanged,
                   [&engine, &app, &configManager]() {
                     loadTranslator(app, configManager.language());
                     engine.retranslate();
                   });

  engine.loadFromModule("Hodeia", "Main");

  return QGuiApplication::exec();
}
