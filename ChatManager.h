#ifndef CHATMANAGER_H
#define CHATMANAGER_H

#include <QObject>
#include <QMap>
#include <QStringList>
#include "ChatModel.h"
#include "Interlocutor.h"
#include "DummyInterlocutor.h" // Pour le debug

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

    // Méthode Q_INVOKABLE pour changer l'interlocuteur depuis QML
    Q_INVOKABLE void switchToInterlocutor(const QString &name);

    ChatModel* chatModel() const { return m_chatModel; }
    QStringList interlocutorNames() const;
    QString activeInterlocutorName() const { return m_activeInterlocutorName; }


signals:
    void interlocutorNamesChanged();
    void activeInterlocutorNameChanged();

private:
    void loadInterlocutors(); // Charge les interlocuteurs (depuis la config, etc.)

    ChatModel* m_chatModel;
    QMap<QString, Interlocutor*> m_interlocutors; // Stocke tous les interlocuteurs par nom
    QString m_activeInterlocutorName;
    QString m_chatFilesPath; // Chemin vers le dossier des fichiers .jsonl
};

#endif // CHATMANAGER_H