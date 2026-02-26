// Begin Source File: ChatManager.h
#ifndef CHATMANAGER_H
#define CHATMANAGER_H

#include "ChatModel.h"
#include "Interlocutor.h"
#include <QMap>
#include <QObject>
#include <QStringList>

// #include "DummyInterlocutor.h" // Pour le debug
#include "InterlocutorConfig.h"
#include "ModelRegistry.h"

class ChatManager : public QObject
{
    Q_OBJECT

public:
    explicit ChatManager(QObject *parent = nullptr);
    ~ChatManager();

    // Propriété pour le ChatModel, exposé à QML
    Q_PROPERTY(ChatModel *chatModel READ chatModel CONSTANT)

    // Propriété pour la liste des noms d'interlocuteurs, pour un ComboBox en QML
    Q_PROPERTY(QStringList interlocutorNames READ interlocutorNames NOTIFY interlocutorNamesChanged)

    // Propriété pour le nom de l'interlocuteur actif
    Q_PROPERTY(QString activeInterlocutorName READ activeInterlocutorName NOTIFY
                   activeInterlocutorNameChanged)

    // Propriétés pour les images du personnage actif
    Q_PROPERTY(QString activeInterlocutorImagePath1 READ activeInterlocutorImagePath1 NOTIFY
                   activeInterlocutorImagePathsChanged)
    Q_PROPERTY(QString activeInterlocutorImagePath2 READ activeInterlocutorImagePath2 NOTIFY
                   activeInterlocutorImagePathsChanged)

    // Propriété pour la configuration en cours d'édition dans l'onglet "Configure"
    Q_PROPERTY(InterlocutorConfig *currentConfig READ currentConfig NOTIFY currentConfigChanged)

    // Propriété pour la liste des Providers disponibles (pour le ComboBox)
    Q_PROPERTY(QStringList availableProviders READ availableProviders CONSTANT)

    // Méthode Q_INVOKABLE pour changer l'interlocuteur depuis QML
    Q_INVOKABLE void switchToInterlocutor(const QString &name);

    // Méthodes Q_INVOKABLE pour l'onglet de configuration
    Q_INVOKABLE void selectConfigToEdit(const QString &name);
    Q_INVOKABLE void createNewConfig();
    Q_INVOKABLE bool saveConfig(InterlocutorConfig *config); // Renvoie true si succès
    Q_INVOKABLE void deleteConfig(const QString &name);

    Q_INVOKABLE void copyToClipboard(const QString &text);

    InterlocutorConfig *currentConfig() const;
    QStringList availableInterlocutorTypes() const;

    ChatModel *chatModel() const { return m_chatModel; }
    QStringList interlocutorNames() const;
    QString activeInterlocutorName() const { return m_activeInterlocutorName; }

    Q_INVOKABLE QStringList modelsForProvider(const QString &provider);
    Q_INVOKABLE void updateConfigWithModel(const QString &modelDisplayName);

    // Gestion des images de personnage
    Q_INVOKABLE QString getInterlocutorImagePath(const QString &name, int index) const;
    Q_INVOKABLE bool setInterlocutorImage(const QString &name, int index, const QUrl &sourceUrl);
    Q_INVOKABLE void clearInterlocutorImage(const QString &name, int index);

    QString activeInterlocutorImagePath1() const { return m_activeImagePath1; }
    QString activeInterlocutorImagePath2() const { return m_activeImagePath2; }

    QStringList availableProviders() const;
    InterlocutorConfig *findCurrentConfig();
    InterlocutorConfig *findConfigByName(const QString &configName);

signals:
    void interlocutorNamesChanged();
    void activeInterlocutorNameChanged(QString &name);
    void activeInterlocutorImagePathsChanged();
    void currentConfigChanged();

private:
    ChatModel *m_chatModel;
    QMap<QString, Interlocutor *> m_interlocutors; // Stocke tous les interlocuteurs par nom
    QString m_activeInterlocutorName;

    QString m_chatFilesPath; // Chemin vers le dossier des fichiers .jsonl
    QString m_activeImagePath1;
    QString m_activeImagePath2;
    void
    refreshActiveImagePaths(const QString &name); // Met à jour les propriétés et émet le signal
    // Logique de persistance
    void saveInterlocutorsToDisk();
    void loadInterlocutorsFromDisk();

    Interlocutor *createInterlocutorFromConfig(InterlocutorConfig *config);
    InterlocutorConfig *m_currentConfig = nullptr; // Pointeur vers la config en cours d'édition
    QList<InterlocutorConfig *> m_allConfigs;      // La liste de toutes les configurations
    ModelRegistry m_modelRegistry;
};

#endif // CHATMANAGER_H
// End Source File: ChatManager.h
