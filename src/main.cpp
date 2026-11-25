#include "configmanager.h"
#include "database.h"
#include "sqlitevec.h"
#include "enginemanager.h"
#include "helpers.h"
#include "llama.h"
#include "mcplibrary.h"
#include "mcpserver.h"
#include "mcpstdio.h"
#include "mtmd-helper.h"
#include "notifier.h"
#include "openalex.h"
#include "querycontroller.h"
#include "summarythreadmanager.h"
#include "thememanager.h"
#include "agentchatcontroller.h"
#include "agentguard.h"
#include "reindexcontroller.h"
#include "zoterocontroller.h"

#include <QDir>
#include <QStandardPaths>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTextStream>
#include <QTimer>
#include <QTranslator>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <cstdio>
#include <cstring>

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
  if (!s_logFile) {
    // The exe dir is not writable in an all-users install (Program Files):
    // fall back to the per-user data dir (%APPDATA%/Hodeia, ~/.local/share/...).
    const QString dataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    s_logPath = dataDir + "/Hodeia.log";
    s_logFile = fopen(s_logPath.toUtf8().constData(), "wb");
  }
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
static void llamaLogCallback(enum ggml_log_level level, const char *text,
                             void *) {
  // Surface backend-load failures and warnings without the model loader's
  // INFO metadata dump or mtmd's per-chunk DEBUG trace (which prints the
  // whole prompt text). GGML_LOG_LEVEL_CONT continues the previous message:
  // keep it only if that message was kept.
  // Also keep the INFO lines giving the real size of each buffer (weights,
  // KV cache, recurrent state, compute) and the resolved flash attention
  // mode: they are what lb_fit_params's estimate is checked against
  // (docs/NOTES-VRAM-FITTING.md).
  static thread_local bool s_lastKept = false;
  if (level != GGML_LOG_LEVEL_CONT) {
    const char *t = text ? text : "";
    s_lastKept = level >= GGML_LOG_LEVEL_WARN ||
                 (level == GGML_LOG_LEVEL_INFO &&
                  (std::strstr(t, "buffer size") ||
                   std::strstr(t, "llama_kv_cache: size") ||
                   std::strstr(t, "flash_attn")));
  }
  if (!s_lastKept)
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
}

// From llama.cpp common/log.h (not staged in external/include): messages
// above this verbosity are dropped by libcommon's own logger ("cmn ..."
// lines), which writes to stderr directly rather than through
// llama_log_set.  LOG_LEVEL_WARN = 2 keeps warnings and errors only.
void common_log_set_verbosity_thold(int verbosity);

static void initLlamaLogging() {
#ifdef Q_OS_WIN
  // UTF-8 console (was done by llama.cpp's common_init(), no longer called).
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif
  llama_log_set(llamaLogCallback, nullptr);
  common_log_set_verbosity_thold(2 /* LOG_LEVEL_WARN */);
  // mtmd (multimodal) and its helpers have their own logger, which by
  // default prints everything — DEBUG included — to stderr.
  mtmd_helper_log_set(llamaLogCallback, nullptr);
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

// ── MCP server mode (Hodeia --mcp) ─────────────────────────────────────────
// An agent (Claude Code, Claude Desktop...) starts the executable with --mcp
// and talks MCP over stdin/stdout.  No window, no QML: a QCoreApplication.
// stdout carries the protocol only, so every log line goes to stderr and to
// Hodeia-mcp.log (a file of its own: the GUI app may be running and writing
// Hodeia.log at the same time).

static bool hasArgument(int argc, char *argv[], const char *arg) {
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], arg) == 0)
      return true;
  return false;
}

static FILE *s_mcpLogFile = nullptr;

static void mcpLog(const QString &line) {
  const QByteArray bytes = line.toUtf8() + '\n';
#ifdef Q_OS_WIN
  // GUI-subsystem build: write to the inherited stderr handle directly.
  HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
  if (h && h != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(h, bytes.constData(), DWORD(bytes.size()), &written, nullptr);
  }
#else
  fwrite(bytes.constData(), 1, size_t(bytes.size()), stderr);
#endif
  if (s_mcpLogFile) {
    fwrite(bytes.constData(), 1, size_t(bytes.size()), s_mcpLogFile);
    fflush(s_mcpLogFile);
  }
}

static int runMcpServer(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  app.setApplicationName("Hodeia");
  app.setApplicationVersion(QStringLiteral(HODEIA_VERSION));

  const QString dataDir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dataDir);
  s_mcpLogFile = fopen((dataDir + "/Hodeia-mcp.log").toUtf8().constData(), "wb");
  qInstallMessageHandler(
      [](QtMsgType type, const QMessageLogContext &, const QString &msg) {
        const char *tag = type == QtDebugMsg      ? "DEBUG"
                          : type == QtInfoMsg     ? "INFO"
                          : type == QtWarningMsg  ? "WARN"
                          : type == QtCriticalMsg ? "CRIT"
                                                  : "FATAL";
        mcpLog(QString("[%1] %2").arg(tag, msg));
        if (type == QtFatalMsg)
          std::abort();
      });
  qInfo() << "[MCP] HodeIA" << HODEIA_VERSION << "MCP server on stdio";
  // llama.cpp (embedding model) logs through the same filter as the app.
  initLlamaLogging();

  int rc = 0;
  {
    // Everything that may log lives in this scope: the log file is closed
    // only after all of it is destroyed.
    McpServer server(QStringLiteral(HODEIA_VERSION));
    server.addTool(
        {QStringLiteral("ping"), QStringLiteral("Ping"),
         QStringLiteral("Check that the HodeIA server answers. Returns its "
                        "version."),
         QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
         [](const QJsonObject &) -> std::expected<QString, QString> {
           return QStringLiteral("pong — HodeIA %1").arg(HODEIA_VERSION);
         }});

    // The library tools (search, list, summary, page, citations).
    McpLibrary library(McpLibrary::defaultPaths());
    library.registerTools(server);

    McpStdioTransport transport(server);
    QObject::connect(&transport, &McpStdioTransport::closed, &app,
                     &QCoreApplication::quit);
    transport.start();
    rc = QCoreApplication::exec();
  }
  qInfo() << "[MCP] exit";
  if (s_mcpLogFile) {
    FILE *f = s_mcpLogFile;
    s_mcpLogFile = nullptr;
    fclose(f);
  }
  return rc;
}

// Main app section. The entry point
int main(int argc, char *argv[]) {
  if (hasArgument(argc, argv, "--mcp"))
    return runMcpServer(argc, argv);
  // An agent's tool hook (see Agents::runGuard): no app, a quick answer.
  for (int i = 1; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], "--agent-guard") == 0)
      return Agents::runGuard(QString::fromLocal8Bit(argv[i + 1]));

#ifdef Q_OS_LINUX
  // Native file dialogs through xdg-desktop-portal.  This is a
  // QGuiApplication (no Qt Widgets), so desktop themes whose dialogs are
  // widget-based (KDE Plasma) can't provide one and QtQuick.Dialogs falls back
  // to its built-in QML dialog — which only supports single selection, so
  // "Add files" (FileDialog.OpenFiles) could pick one PDF at a time.  The
  // portal theme only replaces dialogs; fonts, icons, etc. still come from the
  // desktop's own theme.  Respect an explicit user choice.
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME"))
    qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
#endif

  QGuiApplication app(argc, argv);
  app.setApplicationName("Hodeia");
  app.setApplicationDisplayName("HodeIA");
  app.setApplicationVersion(QStringLiteral(HODEIA_VERSION));
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

  // E5 embedding service (search, ingestion), independent of the UI models;
  // loaded lazily on first use.  Declared first: destroyed last.
  SqliteVecManager embeddings(&configManager);

  // Create DatabaseManager with config reference for project DB resolution
  DatabaseManager databaseManager(&configManager, embeddings);

  // Create the PDF ingestion worker
  EngineController engineController(&configManager, &embeddings);
  // The project an import writes to cannot be deleted or cleared meanwhile.
  QObject::connect(&engineController, &EngineController::ingestionTargetChanged,
                   &databaseManager, &DatabaseManager::setIngestionTarget);

  // Create the Summarizer (orchestrates local/remote backends)
  SummaryController summaryController(&configManager, &databaseManager);

  // Create the Query controller (RAG over the paper library)
  QueryController queryController(&configManager, &databaseManager);

  // Create the Helpers (manages model discovery, summary formatting, PDF
  // export)
  Helpers helpers(&configManager);
  helpers.setDatabase(&databaseManager);

  // Create the OpenAlex reference service (independent background thread)
  OpenAlexClient openAlexClient(&configManager, &databaseManager);
  databaseManager.setOpenAlexClient(&openAlexClient);
  QObject::connect(&summaryController, &SummaryController::summaryFinished,
                   &openAlexClient, &OpenAlexClient::checkEmptyReferences);

  // Summaries an agent saved through the MCP server: their citations too.
  QObject::connect(&databaseManager, &DatabaseManager::summariesChangedElsewhere,
                   &openAlexClient, &OpenAlexClient::checkEmptyReferences);

  // Startup catch-up: fetch references for papers summarized in prior sessions
  QTimer::singleShot(0, &openAlexClient, &OpenAlexClient::checkEmptyReferences);

  // Re-embedding of the chunks of a 0.99rc project (embedded without E5's
  // special tokens; task recorded by the migration), in the background.
  ReindexController reindexController(&databaseManager, &engineController,
                                      embeddings);

  // Conversations with an agent (mode agentique of the Query window).
  AgentChatController agentChat(&configManager, &databaseManager);

  // Zotero connector (reads Zotero, imports through the ingestion)
  ZoteroController zoteroController(&configManager, &databaseManager,
                                    &engineController);
  // Zotero PDFs stored or imported: papers with a DOI get their citations
  // without waiting for a summary.
  QObject::connect(&zoteroController, &ZoteroController::itemsUpdated,
                   &openAlexClient, &OpenAlexClient::checkEmptyReferences);
  // Zotero's metadata first, or not: the paper list shows the other names.
  QObject::connect(&configManager, &ConfigManager::zoteroPriorityChanged,
                   &databaseManager, &DatabaseManager::reloadModels);

  QQmlApplicationEngine engine;
  engine.setUiLanguage(configManager.language());
  engine.rootContext()->setContextProperty("themeManager", &themeManager);
  engine.rootContext()->setContextProperty("configManager", &configManager);
  engine.rootContext()->setContextProperty("databaseManager", &databaseManager);
  engine.rootContext()->setContextProperty("engineController",
                                           &engineController);
  engine.rootContext()->setContextProperty("summaryController",
                                           &summaryController);
  engine.rootContext()->setContextProperty("queryController", &queryController);
  engine.rootContext()->setContextProperty("helpers", &helpers);
  engine.rootContext()->setContextProperty("notifier", Notifier::instance());
  engine.rootContext()->setContextProperty("zoteroController",
                                           &zoteroController);
  engine.rootContext()->setContextProperty("reindexController",
                                           &reindexController);
  engine.rootContext()->setContextProperty("agentChat", &agentChat);
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

  // When the user changes the language in Settings, reload the translator and
  // then retranslate the already-loaded QML with engine.retranslate()
  QObject::connect(&configManager, &ConfigManager::languageChanged,
                   [&engine, &app, &configManager]() {
                     loadTranslator(app, configManager.language());
                     // After the catalog: QML bindings see languageChanged
                     // before this slot, so C++ tr() lists follow uiLanguage.
                     engine.setUiLanguage(configManager.language());
                     engine.retranslate();
                   });

  engine.loadFromModule("Hodeia", "Main");

  return QGuiApplication::exec();
}
