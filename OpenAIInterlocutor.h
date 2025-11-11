// Begin source file OpenAIInterlocutor.h
#pragma once
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
#include "Interlocutor.h"


class OpenAIInterlocutor : public Interlocutor {
  Q_OBJECT
 public:
     explicit OpenAIInterlocutor(
         QString interlocutorName,
         const QString &apiKey, // Secret API Key
         const QUrl &url, // For instance, for OpenAI: "https://api.openai.com/v1/responses"
         const QString &model, // For instance "gpt-4o"
         QObject *parent);

     Q_INVOKABLE void sendRequest(const QList<ChatMessage> &history,
                                  const QStringList &attachmentFileIds) override;
     void setSystemPrompt(const QString &systemPrompt) override;

     // Les signaux ci-dessous sont hérités de la classe mère `Interlocutor`
     // signals:
     //     void responseReceived(const QJsonObject &response);
     //     void errorOccurred(const QString &error);
     void uploadFile(const QByteArray &content, const QString &purpose) override;
     void deleteFile(const QString &fileId) override;

 private:
     QUrl m_url;
     QString m_apiKey;
     QString m_model;
     QNetworkAccessManager *m_manager;
     QJsonObject m_systemMsg;
     const int REQUEST_TIMEOUT_MS = 100000;
     QMap<QNetworkReply *, QTimer *> m_requestTimers;
};
// End source file OpenAIInterlocutor.h
