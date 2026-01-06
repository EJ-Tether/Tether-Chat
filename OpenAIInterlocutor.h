// Begin source file OpenAIInterlocutor.h
#pragma once
#include "Interlocutor.h"
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>

class OpenAIInterlocutor : public Interlocutor
{
    Q_OBJECT
public:
    explicit OpenAIInterlocutor(
        QString interlocutorName,
        const QString &apiKey, // Secret API Key
        const QUrl &url,       // For instance, for OpenAI: "https://api.openai.com/v1/responses"
        const QString &model,  // For instance "gpt-4o"
        QObject *parent);

    void sendRequest(const QList<ChatMessage> &history, const QString &ancientMemory,
                     const InterlocutorReply::Kind kind,
                     const QStringList &attachmentFileIds) override;
    ;

    // Les signaux ci-dessous sont hérités de la classe mère `Interlocutor`
    // signals:
    //     void responseReceived(const QJsonObject &response);
    //     void errorOccurred(const QString &error);
    void uploadFile(QString fileName, const QByteArray &content, const QString &purpose) override;
    void deleteFile(const QString &fileId) override;
    void countTokensForFiles(const QStringList &fileIds) override;

private:
    QUrl m_url;
    QString m_apiKey;
    QString m_model;
    QNetworkAccessManager *m_manager;
    const int REQUEST_TIMEOUT_MS = 360000;
    QMap<QNetworkReply *, QTimer *> m_requestTimers;
};
// End source file OpenAIInterlocutor.h
