// Begin source file GoogleAIInterlocutor.h
#ifndef GOOGLEAIINTERLOCUTOR_H
#define GOOGLEAIINTERLOCUTOR_H

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QUrl>
#include "Interlocutor.h"

class GoogleAIInterlocutor : public Interlocutor
{
    Q_OBJECT
public:
    explicit GoogleAIInterlocutor(QString interlocutorName,
                                  const QString &apiKey,
                                  const QUrl &url,
                                  QObject *parent = nullptr);

    Q_INVOKABLE void sendRequest(const QList<ChatMessage> &history,
                                 const QStringList &attachmentFileIds) override;
    void setSystemPrompt(const QString &systemPrompt) override;

private:
    QString m_apiKey;
    QUrl m_url;
    QNetworkAccessManager *m_manager;
    QString m_systemPrompt;
};

#endif // GOOGLEAIINTERLOCUTOR_H
// End source file GoogleAIInterlocutor.h
