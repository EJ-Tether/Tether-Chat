// Begin Source File ChatModel.h
#ifndef CHATMODEL_H
#define CHATMODEL_H

#include <QAbstractListModel>
#include <QFile>
#include <QHash>
#include <QJSEngine> // For QML_DECLARE_TYPE
#include <QList>
#include <QQmlEngine> // For QQmlEngine::registerUncreatableType
#include <QTextStream>

#include "ChatMessage.h"
#include "Interlocutor.h" // Ou DummyInterlocutor.h pour le debug
#include "InterlocutorConfig.h"
#include "ManagedFile.h"

class ChatModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum ChatMessageRoles
    {
        IsLocalMessageRole = Qt::UserRole + 1,
        TextRole,
        TimestampRole,
        PromptTokensRole,
        CompletionTokensRole,
        RoleRole, // "user" ou "assistant"
        IsTypingIndicatorRole,
        IsErrorRole
    };
    Q_ENUM(ChatMessageRoles)

    explicit ChatModel(QObject *parent = nullptr);
    void setInterlocutor(Interlocutor *interlocutor);

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
    Q_PROPERTY(QString currentChatFilePath READ currentChatFilePath WRITE setCurrentChatFilePath
                   NOTIFY currentChatFilePathChanged)
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

    Q_PROPERTY(QList<QObject *> managedFiles READ managedFiles NOTIFY managedFilesChanged)
    QList<QObject *> managedFiles() const;
    Q_INVOKABLE void uploadUserFile(const QUrl &fileUrl);
    Q_INVOKABLE void deleteUserFile(int index);

signals:
    void currentChatFilePathChanged();
    void liveMemoryTokensChanged();
    void cumulativeTokenCostChanged();
    void chatMessageAdded(const ChatMessage &message);
    void curationNeeded();               // Signal pour indiquer qu'une curation est nécessaire
    void curationFinished(bool success); // Signal utile pour notifier l'UI
    void isWaitingForReplyChanged();
    void managedFilesChanged();

public slots:
    void onInterlocutorError(const QString &message);

private slots:
    void onInterlocutorReply(const InterlocutorReply &reply);
    void onFileUploaded(const QString &fileId, const QString &purpose);
    void onFileDeleted(const QString &fileId, bool success);
    void onFileUploadFailed(const QString &error);
    void onFileTokenCountReady(int count);

private:
    void addMessage(const ChatMessage &message); // Add a message to the model *and*
    // the jsonl live memory file
    void updateLiveMemoryEstimate();
    void handleNormalReply(const InterlocutorReply &reply);
    void handleCurationReply(const InterlocutorReply &reply);

    void checkCurationThreshold();
    void triggerCuration();

    InterlocutorConfig *findCurrentConfig();

    void loadManagedFiles();
    void saveManagedFiles() const;
    QString getManagedFilesPath() const;

    QList<ChatMessage> m_messages;
    Interlocutor *m_interlocutor; // L'interlocuteur réel ou bidon
    QString m_currentChatFilePath;
    int m_liveMemoryTokens = 0;
    int m_cumulativeTokenCost = 0;

public:
    Q_INVOKABLE void setCurationThresholds(int triggerTokens, int targetTokens);

private:
    // Seuil de tokens pour la mémoire active (par exemple, 100K)
    int m_curationTargetTokenCount = 85000;
    // Seuil de déclenchement de la curation (par exemple, 120K)
    int m_curationTriggerTokenCount = 100000;
    void rewriteChatFile();

    // Gestion de la curation
    QString getOlderMemoryFilePath() const;       // Donne le chemin du fichier de mémoire ancienne
    QString loadOlderMemory();                    // Charge le contenu de la mémoire ancienne
    void saveOlderMemory(const QString &content); // Sauvegarde la mémoire ancienne

    // Flags pour gérer le processus de curation asynchrone
    bool m_isCurationInProgress = false;
    bool m_isWaitingForCurationResponse = false;
    QString m_liveMemoryFileIdForCuration;
    QString m_oldAncientMemoryFileIdToDelete;

    // Code pour gérer l'état d'attente de réponse
    void setWaitingForReply(bool waiting); // Setter privé pour gérer l'état
    bool m_isWaitingForReply = false;

    QList<ManagedFile *> m_managedFiles;
    void removeTypingIndicator();
    bool m_expectingContinuation = false;
    bool m_expectingFileTokenCount = false;
    int m_pendingFileTokenCount = 0;
};

#endif // CHATMODEL_H
// End Source File ChatModel.h
