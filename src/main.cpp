#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QIcon>
#include <QQmlContext>
#include "headers/thememanager.h"


int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/Resources/paperize.png"));
    ThemeManager themeManager;
    // Adjust path to where your JSON folder sits relative to the executable
    themeManager.loadThemesFromFolder(app.applicationDirPath() + "/ui/themes");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("themeManager", &themeManager);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("Paperize_CPP", "Main");

    return QGuiApplication::exec();
}
