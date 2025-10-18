// Begin Source File ChatModel.h
#ifndef CHATMODEL_H
#define CHATMODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QHash>
#include <QFile>
#include <QTextStream>
#include <QJSEngine> // For QML_DECLARE_TYPE
#include <QQmlEngine> // For QQmlEngine::registerUncreatableType

#include "ChatMessage.h"
#include "Interlocutor.h" // Ou DummyInterlocutor.h pour le debug
#include "InterlocutorConfig.h"

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
        RoleRole, // "user" ou "assistant"
        IsTypingIndicatorRole
    };
    Q_ENUM(ChatMessageRoles)

    explicit ChatModel(QObject *parent = nullptr);
    void setInterlocutor(Interlocutor* interlocutor) ;

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

    // Représente la taille du contexte actuel pour la curation
    Q_PROPERTY(int liveMemoryTokens READ liveMemoryTokens NOTIFY liveMemoryTokensChanged)
    // Représente le coût cumulatif total pour l'utilisateur
    Q_PROPERTY(int cumulativeTokenCost READ cumulativeTokenCost NOTIFY cumulativeTokenCostChanged)

    int liveMemoryTokens() const { return m_liveMemoryTokens; }
    int cumulativeTokenCost() const { return m_cumulativeTokenCost; }
    Q_INVOKABLE void resetTokenCost(); // méthode pour le bouton "Reset"

    Q_PROPERTY(bool isWaitingForReply READ isWaitingForReply NOTIFY isWaitingForReplyChanged)
    bool isWaitingForReply() const { return m_isWaitingForReply; }

signals:
    void currentChatFilePathChanged();
    void liveMemoryTokensChanged();
    void cumulativeTokenCostChanged();
    void chatMessageAdded(const ChatMessage &message);
    void chatError(const QString &error);
    void curationNeeded(); // Signal pour indiquer qu'une curation est nécessaire
    void curationFinished(bool success); // Signal utile pour notifier l'UI
    void isWaitingForReplyChanged();

private slots:
    void handleInterlocutorResponse(const QJsonObject &response);
    void handleInterlocutorError(const QString &error);
    void onLiveMemoryUploadedForCuration(const QString &fileId, const QString &purpose);
    void onNewAncientMemoryUploaded(const QString &newFileId, const QString &purpose);
    void onOldAncientMemoryDeleted(const QString &fileId, bool success);
    void onCurationUploadFailed(const QString &error);

private:
    void addMessage(const ChatMessage &message); // Add a message to the model *and* the jsonl live memory file
    void updateLiveMemoryEstimate();
    void checkCurationThreshold();
    void triggerCuration();
    InterlocutorConfig *findCurrentConfig();

    QList<ChatMessage> m_messages;
    Interlocutor *m_interlocutor; // L'interlocuteur réel ou bidon
    QString m_currentChatFilePath;
    int m_liveMemoryTokens = 0;
    int m_cumulativeTokenCost = 0;

    // Seuil de tokens pour la mémoire active (par exemple, 100K)
    const int BASE_LIVE_MEMORY_TOKENS = 5000 ;/* Reduced for debug purposes! */ // ORIGINAL VALUE WAS : 100000;
    // Seuil de déclenchement de la curation (par exemple, 120K)
    const int CURATION_TRIGGER_TOKENS = 6000 ; /* Reduced for debug purposes! */ // ORIGINAL VALUE WAS : 120000;
    void rewriteChatFile();

    // Gestion de la curation
    QString getOlderMemoryFilePath() const; // Donne le chemin du fichier de mémoire ancienne
    QString loadOlderMemory(); // Charge le contenu de la mémoire ancienne
    void saveOlderMemory(const QString& content); // Sauvegarde la mémoire ancienne

    // Flags pour gérer le processus de curation asynchrone
    bool m_isCurationInProgress = false;
    bool m_isWaitingForCurationResponse = false;
    QString m_liveMemoryFileIdForCuration;
    QString m_oldAncientMemoryFileIdToDelete;

    // Code pour gérer l'état d'attente de réponse
    void setWaitingForReply(bool waiting); // Setter privé pour gérer l'état
    bool m_isWaitingForReply = false;
};

#endif // CHATMODEL_H
// End Source File ChatModel.h
