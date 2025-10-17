// Begin source file OpenAIInterlocutor.cpp
#include "OpenAIInterlocutor.h"
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

OpenAIInterlocutor::OpenAIInterlocutor(QString interlocutorName,
                                       const QString &apiKey,
                                       const QUrl &url,
                                       const QString &model,
                                       QObject *parent)
    : Interlocutor(interlocutorName, parent)
    , m_apiKey(apiKey)
    , m_url(url)
    , m_model(model)
{
    m_manager = new QNetworkAccessManager(this);
}

void OpenAIInterlocutor::setSystemPrompt(const QString &systemPrompt)
{
    if (!systemPrompt.isEmpty()) {
        m_systemMsg["role"] = "system";
        m_systemMsg["content"] = systemPrompt;
    } else {
        m_systemMsg = QJsonObject(); // Vider si le prompt est vide
    }
}

void OpenAIInterlocutor::sendRequest(const QList<ChatMessage> &history)
{
    QNetworkRequest request(m_url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());

    // --- Construction du payload JSON pour OpenAI ---
    QJsonObject payload;
    payload["model"] = m_model;

    QJsonArray messagesArray;
    // 1. Ajouter le message système s'il existe
    if (!m_systemMsg.isEmpty()) {
        messagesArray.append(m_systemMsg);
    }

    // 2. Ajouter tout l'historique
    for (const ChatMessage &msg : history) {
        QJsonObject messageObject;
        messageObject["role"] = msg.isLocalMessage() ? "user" : "assistant";
        messageObject["content"] = msg.text();
        messagesArray.append(messageObject);
    }

    payload["messages"] = messagesArray;

    // Pour le streaming, si vous l'implémentez plus tard :
    // payload["stream"] = true;

    QByteArray data = QJsonDocument(payload).toJson();
    QNetworkReply *reply = m_manager->post(request, data);

    // ... (Votre gestion de timeout et de la réponse est déjà bonne et reste inchangée)
    // ...
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        // ... (gestion du timeout)
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument jsonResponse = QJsonDocument::fromJson(reply->readAll());
            if (!jsonResponse.isNull() && jsonResponse.isObject()) {
                emit responseReceived(jsonResponse.object());
            } else {
                emit errorOccurred("Invalid JSON response from OpenAI API.");
            }
        } else {
            emit errorOccurred("API Error: " + reply->errorString());
        }
        reply->deleteLater();
    });
}
