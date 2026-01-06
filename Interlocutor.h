// Begin source file Interlocutor.h
#ifndef INTERLOCUTOR_H
#define INTERLOCUTOR_H

#include "ChatMessage.h"
#include "InterlocutorReply.h"
#include <QJsonObject>
#include <QObject>


class Interlocutor : public QObject
{
    Q_OBJECT
public:
    explicit Interlocutor(QString interlocutorName, QObject *parent = nullptr)
        : m_interlocutorName(interlocutorName)
        , QObject(parent)
    {
    }
    virtual ~Interlocutor() {}

    virtual void sendRequest(const QList<ChatMessage> &history, const QString &ancientMemory,
                             const InterlocutorReply::Kind kind,
                             const QStringList &attachmentFileIds) = 0;

    virtual void uploadFile(QString fileName, const QByteArray &content,
                            const QString &purpose) = 0;
    virtual void deleteFile(const QString &fileId) = 0;
    virtual void setSystemPrompt(const QString &systemPrompt) { m_systemPrompt = systemPrompt; }
    virtual void countTokensForFiles(const QStringList &fileIds)
    {
        Q_UNUSED(fileIds);
        // Default implementation does nothing.
    }

signals:
    void replyReady(const InterlocutorReply &reply);
    void errorOccurred(const QString &error);

    // Uniquement pour les PDF uploadés par l'utilisateur :
    void fileUploaded(const QString &fileId, const QString &purpose);
    void fileUploadFailed(const QString &error);
    void fileDeleted(const QString &fileId, bool success);
    void fileTokenCountReady(int count);

protected:


    // m_interlocutorName: That's the name that the user entered in the 'configuration' tab.
    // Important for:
    //   (1) naming the jsonl file of the current discussion and the other informations
    //   (2) being displayed in the combo box for chosing the current interlocutor
    QString m_interlocutorName;
    QString m_systemPrompt; // Copie locale du system prompt changé par le ChatManager à chaque
                            // changement d'interlocuteur
};

#endif // INTERLOCUTOR_H
// End source file Interlocutor.h
