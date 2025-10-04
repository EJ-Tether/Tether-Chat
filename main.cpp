#include <QGUIApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <qobject.h>

#include "ChatMessage.h"
#include "ChatModel.h"
#include "Interlocutor.h"
#include "DummyInterlocutor.h"
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

    qmlRegisterType<ChatMessage>("Tether", 1, 0, "ChatMessage");
    qmlRegisterType<ChatModel>("Tether", 1, 0, "ChatModel");

    // DEBUG CODE : we create the current interlocutor as a DUMMY INTERLOCUTOR. Later in the project, the current
    // Interlocutor will be changed depending on the choice in the Configuration menu, that will be made persistent
    // between restarts by storing the last current interlocutor in the .settings
    Interlocutor *llmInterlocutor = new DummyInterlocutor(&app);

    engine.rootContext()->setContextProperty("_currentInterlocutor", llmInterlocutor);

    // Persistent settings management:
    // WORK IN PROGRESS: IMPLEMENT THE C++ OBJECT MANAGING THE APP SETTINGS HERE:
    Settings settings;
    qmlRegisterUncreatableType<Settings>(APP_NAME, MAJOR_VERSION, MINOR_VERSION , "Settings", "Model data");
    engine.rootContext()->setContextProperty("_settings", &settings);

    // Translations: Managing the change in the current language
    QObject::connect(&settings, &Settings::retranslate, &app, [&engine, &settings]() {
        // settings.set_changingLanguage(true);
        engine.retranslate();
        //settings.set_changingLanguage(false);
    });

    // Temporary architecture: we just set ONE current interlocutor.
    // TODO: in further versions, we'll have a `ListOfInterlocutors` class that contains the list of all known AI chat partners,
    // This list is a collection of abstract classes `Interlocutor`. `Interlocutor` is an abstract classe / Interface, that will
    // be derived in OpenAIInterlocutor, AnthropicInterlocutor, DummyInterlocutor (for debut), LocalInterlocutor (for
    // self-hosting), etc...
    // NB: When the currentInterlocutor will change, a signal will activate the slot currentInterlocutorChanged to update the
    // GUI dynamically
    Interlocutor *currentInterlocutor = nullptr ;
    qmlRegisterUncreatableType<Settings>(APP_NAME, MAJOR_VERSION, MINOR_VERSION , "Interlocutor", "Model data");
    engine.rootContext()->setContextProperty("_currentInterlocutor", currentInterlocutor);


    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("Tether", "Main");
    return app.exec();
}
