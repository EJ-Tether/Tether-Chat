#ifndef CHATMODEL_H
#define CHATMODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QHash>
#include <QFile>
#include <QTextStream>
#include <QJSEngine> // Pour QML_DECLARE_TYPE
#include <QQmlEngine> // Pour QQmlEngine::registerUncreatableType

#include "ChatMessage.h"
#include "Interlocutor.h" // Ou DummyInterlocutor.h pour le debug


class ChatModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum ChatMessageRoles {
        IsLocalMessageRole = Qt::UserRole + 1,
        TextRole,
        TimestampRole,
        PromptTokensRole,
        CompletionTokensRole,
        RoleRole // "user" ou "assistant"
    };
    Q_ENUM(ChatMessageRoles)

    explicit ChatModel(Interlocutor *interlocutor, QObject *parent = nullptr);
    ~ChatModel();

    // Méthodes de QAbstractListModel
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Méthodes Q_INVOKABLE pour QML
    Q_INVOKABLE void sendMessage(const QString &message);
    Q_INVOKABLE void loadChat(const QString &filePath);
    Q_INVOKABLE void saveChat();
    Q_INVOKABLE void clearChat();

    // Propriétés pour l'état du modèle
    Q_PROPERTY(QString currentChatFilePath READ currentChatFilePath WRITE setCurrentChatFilePath NOTIFY currentChatFilePathChanged)
    QString currentChatFilePath() const { return m_currentChatFilePath; }
    void setCurrentChatFilePath(const QString &path);

    Q_PROPERTY(int totalTokens READ totalTokens NOTIFY totalTokensChanged)
    int totalTokens() const { return m_totalTokens; }

signals:
    void currentChatFilePathChanged();
    void totalTokensChanged();
    void chatMessageAdded(const ChatMessage &message);
    void chatError(const QString &error);
    void curationNeeded(); // Signal pour indiquer qu'une curation est nécessaire

private slots:
    void handleInterlocutorResponse(const QJsonObject &response);
    void handleInterlocutorError(const QString &error);

private:
    void addMessage(const ChatMessage &message); // Ajoute un message au modèle et au fichier
    void updateTokenCount();
    void checkCurationThreshold();
    void triggerCuration(); // Placeholder pour la logique de curation

    QList<ChatMessage> m_messages;
    Interlocutor *m_interlocutor; // L'interlocuteur réel ou bidon
    QString m_currentChatFilePath;
    int m_totalTokens;

    // Seuil de tokens pour la mémoire active (par exemple, 100K)
    const int MAX_LIVE_MEMORY_TOKENS = 100000;
    // Seuil de déclenchement de la curation (par exemple, 120K)
    const int CURATION_TRIGGER_TOKENS = 120000;
};

#endif // CHATMODEL_H
