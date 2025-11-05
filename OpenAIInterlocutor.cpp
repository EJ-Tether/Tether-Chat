// Begin source file OpenAIInterlocutor.cpp
#include "OpenAIInterlocutor.h"
#include <QDebug>
#include <QHttpMultiPart>
#include <QHttpPart>
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

void OpenAIInterlocutor::sendRequest(const QList<ChatMessage> &history,
                                     const QStringList &attachmentFileIds)
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

    // On rajouter la Ancient History en fichier attaché sur le dernier message utilisateur
    if (!history.isEmpty()) {
        QJsonObject lastUserMessage = messagesArray.last().toObject();
        messagesArray.removeLast(); // On le retire pour le modifier

        if (!attachmentFileIds.isEmpty()) {
            QJsonArray attachmentsArray;
            for (const QString &fileId : attachmentFileIds) {
                QJsonObject attachment;
                attachment["file_id"] = fileId;
                // Le "tool" pour les pièces jointes standard est "file_search" (anciennement "retrieval")
                QJsonObject tool;
                tool["type"] = "file_search";
                attachment["tools"] = QJsonArray({tool});
                attachmentsArray.append(attachment);
            }
            lastUserMessage["attachments"] = attachmentsArray;
        }
        messagesArray.append(lastUserMessage);
    }

    payload["messages"] = messagesArray;

    payload["messages"] = messagesArray;

    // Pour le streaming, si vous l'implémentez plus tard :
    // payload["stream"] = true;

    QByteArray data = QJsonDocument(payload).toJson();
    QNetworkReply *reply = m_manager->post(request, data);

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

void OpenAIInterlocutor::uploadFile(const QByteArray &content, const QString &purpose)
{
    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart purposePart;
    purposePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                          QVariant("form-data; name=\"purpose\""));
    purposePart.setBody(purpose.toUtf8());

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant("form-data; name=\"file\"; filename=\"memory.txt\""));
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("text/plain"));
    filePart.setBody(content);

    multiPart->append(purposePart);
    multiPart->append(filePart);

    QUrl url("https://api.openai.com/v1/files");
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());

    QNetworkReply *reply = m_manager->post(request, multiPart);
    multiPart->setParent(reply); // Important pour la gestion de la mémoire

    connect(reply, &QNetworkReply::finished, this, [this, reply, purpose]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
            QString fileId = response["id"].toString();
            if (!fileId.isEmpty()) {
                qDebug() << "File uploaded successfully. ID:" << fileId;
                emit fileUploaded(fileId, purpose);
            } else {
                qWarning() << "File upload failed, response:" << response;
                emit fileUploadFailed("Could not get File ID from API response.");
            }
        } else {
            qWarning() << "File upload API error:" << reply->errorString();
            emit fileUploadFailed(reply->errorString());
        }
        reply->deleteLater();
    });
}

void OpenAIInterlocutor::deleteFile(const QString &fileId)
{
    QUrl url("https://api.openai.com/v1/files/" + fileId);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());

    QNetworkReply *reply = m_manager->deleteResource(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, fileId]() {
        bool success = false;
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
            if (response["deleted"].toBool()) {
                qDebug() << "File deleted successfully. ID:" << fileId;
                success = true;
            }
        } else {
            qWarning() << "File delete API error:" << reply->errorString();
        }
        emit fileDeleted(fileId, success);
        reply->deleteLater();
    });
}
// End source file OpenAIInterlocutor.cpp
