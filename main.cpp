// Beging source file main.cpp
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "ChatManager.h" // Inclure le nouveau manager
#include "InterlocutorConfig.h"
#include "ManagedFile.h"
#include "settings.h"

#define APP_NAME "TetherChat"
#define MAJOR_VERSION 0
#define MINOR_VERSION 5

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    qputenv("QT_QUICK_CONTROLS_STYLE", "Material");
    QGuiApplication::setApplicationName("TetherChat");
    QQmlApplicationEngine engine;

    // --- Enregistrement des types QML ---
    // Ces types doivent être connus de QML, mais on ne les crée pas depuis QML.
    qmlRegisterUncreatableType<ChatModel>(APP_NAME, MAJOR_VERSION, MINOR_VERSION, "ChatModel", "Cannot create ChatModel in QML.");
    qmlRegisterUncreatableType<Settings>(APP_NAME, MAJOR_VERSION, MINOR_VERSION , "Settings", "Cannot create Settings in QML.");
    // Pas besoin d'enregistrer ChatMessage s'il n'est utilisé que dans le modèle
    qmlRegisterType<InterlocutorConfig>(APP_NAME, MAJOR_VERSION, MINOR_VERSION, "InterlocutorConfig");
    qmlRegisterType<ManagedFile>(APP_NAME, MAJOR_VERSION, MINOR_VERSION, "ManagedFile");
    qmlRegisterUncreatableMetaObject(ManagedFile::staticMetaObject,
                                     APP_NAME,
                                     MAJOR_VERSION,
                                     MINOR_VERSION,
                                     "ManagedFile",
                                     "Cannot create ManagedFile in QML, only expose enums");

    // --- Création des objets principaux ---

    // 1. Le ChatManager est le point d'entrée principal
    ChatManager chatManager;
    engine.rootContext()->setContextProperty("_chatManager", &chatManager);

    // 2. L'objet Settings pour la configuration
    Settings settings;
    engine.rootContext()->setContextProperty("_settings", &settings);

    QObject::connect(&settings, &Settings::retranslate, &app, [&engine, &settings]() {
        engine.retranslate();
    });

    // --- Démarrage de l'engine QML ---
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("Tether", "Main");

    return app.exec();
}
// End source file main.cpp
