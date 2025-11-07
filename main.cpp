// Beging source file main.cpp
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
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

    app.setOrganizationName("EJ-Tether"); // Remplace par ton nom ou un pseudo
    app.setApplicationName("Tether");

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

    // 1. Créer l'objet Settings d'abord
    Settings settings;
    engine.rootContext()->setContextProperty("_settings", &settings);

    // 2. Le ChatManager est le point d'entrée principal
    ChatManager chatManager;
    engine.rootContext()->setContextProperty("_chatManager", &chatManager);


    QObject::connect(&settings, &Settings::retranslate, &app, [&engine, &settings]() {
        engine.retranslate();
    });

    // Quand l'interlocuteur actif du ChatManager change, on le dit à Settings pour qu'il le sauvegarde.
    QObject::connect(&chatManager, &ChatManager::activeInterlocutorNameChanged,
                     &settings, &Settings::setLastUsedInterlocutor);

    // 4. Utiliser la valeur de Settings pour initialiser ChatManager
    // On demande à settings quel était le dernier interlocuteur utilisé.
    QString lastInterlocutor = settings.lastUsedInterlocutor();
    if (!lastInterlocutor.isEmpty()) {
        // Si on en a un, on dit au ChatManager de s'activer dessus.
        // On utilise QTimer::singleShot pour s'assurer que l'UI est prête avant de faire le switch.
        QTimer::singleShot(0, &chatManager, [lastInterlocutor, &chatManager](){
            chatManager.switchToInterlocutor(lastInterlocutor);
        });
    }

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
