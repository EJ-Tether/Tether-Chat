// Begin source file Interlocutor.h
#ifndef INTERLOCUTOR_H
#define INTERLOCUTOR_H

#include <QJsonObject>
#include <QObject>
#include "ChatMessage.h"

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

    Q_INVOKABLE virtual void sendRequest(const QList<ChatMessage> &history,
                                         const QStringList &attachmentFileIds = {})
        = 0;
    virtual void uploadFile(const QByteArray &content, const QString &purpose) = 0;
    virtual void deleteFile(const QString &fileId) = 0;
    virtual void setSystemPrompt(const QString &systemPrompt) {}

signals:
    void responseReceived(const QJsonObject &response);
    void errorOccurred(const QString &error);
    void fileUploaded(const QString &fileId, const QString &purpose);
    void fileDeleted(const QString &fileId, bool success);
    void fileUploadFailed(const QString &error);

private:
    // m_interlocutorName: That's the name that the user entered in the 'configuration' tab.
    // Important for:
    //   (1) naming the jsonl file of the current discussion and the other informations
    //   (2) being displayed in the combo box for chosing the current interlocutor
    QString m_interlocutorName;
};

#endif // INTERLOCUTOR_H
// End source file Interlocutor.h
