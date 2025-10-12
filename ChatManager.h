// Begin Source File: ChatManager.h
#ifndef CHATMANAGER_H
#define CHATMANAGER_H

#include <QObject>
#include <QMap>
#include <QStringList>
#include "ChatModel.h"
#include "Interlocutor.h"
//#include "DummyInterlocutor.h" // Pour le debug
#include "InterlocutorConfig.h"

class ChatManager : public QObject
{
    Q_OBJECT

public:
    explicit ChatManager(QObject *parent = nullptr);
    ~ChatManager();

    // Propriété pour le ChatModel, exposé à QML
    Q_PROPERTY(ChatModel* chatModel READ chatModel CONSTANT)

    // Propriété pour la liste des noms d'interlocuteurs, pour un ComboBox en QML
    Q_PROPERTY(QStringList interlocutorNames READ interlocutorNames NOTIFY interlocutorNamesChanged)

    // Propriété pour le nom de l'interlocuteur actif
    Q_PROPERTY(QString activeInterlocutorName READ activeInterlocutorName NOTIFY activeInterlocutorNameChanged)


    // Propriété pour la configuration en cours d'édition dans l'onglet "Configure"
    Q_PROPERTY(InterlocutorConfig* currentConfig READ currentConfig NOTIFY currentConfigChanged)

    // Propriété pour la liste des types d'interlocuteurs disponibles (pour le ComboBox)
    Q_PROPERTY(QStringList availableInterlocutorTypes READ availableInterlocutorTypes CONSTANT)

    // Méthode Q_INVOKABLE pour changer l'interlocuteur depuis QML
    Q_INVOKABLE void switchToInterlocutor(const QString &name);

    // Méthodes Q_INVOKABLE pour l'onglet de configuration
    Q_INVOKABLE void selectConfigToEdit(const QString &name);
    Q_INVOKABLE void createNewConfig();
    Q_INVOKABLE bool saveConfig(InterlocutorConfig* config); // Renvoie true si succès
    Q_INVOKABLE void deleteConfig(const QString &name);

    InterlocutorConfig* currentConfig() const;
    QStringList availableInterlocutorTypes() const;

    ChatModel* chatModel() const { return m_chatModel; }
    QStringList interlocutorNames() const;
    QString activeInterlocutorName() const { return m_activeInterlocutorName; }


signals:
    void interlocutorNamesChanged();
    void activeInterlocutorNameChanged();
    void currentConfigChanged();

private:
    void loadInterlocutors(); // Charge les interlocuteurs (depuis la config, etc.)

    ChatModel* m_chatModel;
    QMap<QString, Interlocutor*> m_interlocutors; // Stocke tous les interlocuteurs par nom
    QString m_activeInterlocutorName;

    QString m_chatFilesPath; // Chemin vers le dossier des fichiers .jsonl
    // Logique de persistance
    void saveInterlocutorsToDisk();
    void loadInterlocutorsFromDisk();

    Interlocutor* createInterlocutorFromConfig(InterlocutorConfig* config);
    InterlocutorConfig* m_currentConfig = nullptr; // Pointeur vers la config en cours d'édition
    QList<InterlocutorConfig*> m_allConfigs; // La liste de toutes les configurations
};

#endif // CHATMANAGER_H
// End Source File: ChatManager.h
