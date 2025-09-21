#include <QGUIApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <qobject.h>

#include "Settings.h"

#define APP_NAME "TetherChat"
#define MAJOR_VERSION 0
#define MINOR_VERSION 5

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    qputenv("QT_QUICK_CONTROLS_STYLE", "Material");
    QGuiApplication::setApplicationName("TetherChat");
    QQmlApplicationEngine engine;

// Activate/deactivate the console message display with the macro CONSOLE_MESSAGES
#if defined(CONSOLE_MESSAGES)
    qmlRegisterUncreatableType<ConsoleMessages>(APP_NAME, MAJOR_VERSION, MINOR_VERSION, "ConsoleMessages", "Model data");
    engine.rootContext()->setContextProperty("_consoleMessages", &consoleMessages);
    qInstallMessageHandler(customMessageHandler);
#endif

    // Persistent settings management:
    // WORK IN PROGRESS: IMPLEMENT THE C++ OBJECT MANAGING THE APP SETTINGS HERE:
    Settings settings;
    qmlRegisterUncreatableType<Settings>(APP_NAME, MAJOR_VERSION, MINOR_VERSION , "Settings", "Model data");
    engine.rootContext()->setContextProperty("_settings", &settings);
    QObject::connect(&settings, &Settings::retranslate, &app, [&engine, &settings]() {
        // settings.set_changingLanguage(true);
        engine.retranslate();
        //settings.set_changingLanguage(false);
    });

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("Tether", "Main");
    return app.exec();
}
