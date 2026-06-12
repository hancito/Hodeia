#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QIcon>
#include <QQmlContext>
#include "headers/thememanager.h"
#include "headers/configmanager.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/Resources/paperize.png"));

    // Create ConfigManager
    ConfigManager configManager;

    // Create ThemeManager and adjust path to where your JSON folder sits relative to the executable
    ThemeManager themeManager;
    themeManager.loadThemesFromFolder(app.applicationDirPath() + "/ui/themes");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("themeManager", &themeManager);
    engine.rootContext()->setContextProperty("configManager", &configManager);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("Paperize_CPP", "Main");

    return QGuiApplication::exec();
}
