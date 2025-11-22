// Begin source file GoogleAIInterlocutor.h
#ifndef GOOGLEAIINTERLOCUTOR_H
#define GOOGLEAIINTERLOCUTOR_H

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QDebug>
#include <QMimeDatabase>

#include "Interlocutor.h"

class GoogleAIInterlocutor : public Interlocutor
{
    Q_OBJECT
public:
    explicit GoogleAIInterlocutor(QString interlocutorName,
                                  const QString &apiKey,
                                  const QUrl &url,
                                  QObject *parent = nullptr);

    void sendRequest(
        const QList<ChatMessage> &history,
        const QString& ancientMemory,
        InterlocutorReply::Kind kind,
        const QStringList &attachmentFileIds) override;
    void uploadFile(QString fileName, const QByteArray &content, const QString &purpose) override;
    void deleteFile(const QString &fileId) override;

private:
    QString m_apiKey;
    QUrl m_url;
    QNetworkAccessManager *m_manager;
};

#endif // GOOGLEAIINTERLOCUTOR_H
// End source file GoogleAIInterlocutor.h
