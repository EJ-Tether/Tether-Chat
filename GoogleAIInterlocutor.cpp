// Begin GoogleAIInterlocutor.cpp
#include "GoogleAIInterlocutor.h"
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QMimeDatabase>
#include <QFileInfo>

GoogleAIInterlocutor::GoogleAIInterlocutor(QString interlocutorName,
                                           const QString &apiKey,
                                           const QUrl &url,
                                           QObject *parent)
    : Interlocutor(interlocutorName, parent)
    , m_apiKey(apiKey)
    , m_url(url)
{
    m_manager = new QNetworkAccessManager(this);
}

void GoogleAIInterlocutor::sendRequest(
    const QList<ChatMessage> &history,
    const QString& ancientMemory,
    InterlocutorReply::Kind kind,
    const QStringList &attachmentFileIds)
{
    // L'URL de l'API v1beta de Gemini nécessite la clé en paramètre
    QUrl requestUrl(m_url);
    QUrlQuery query;
    query.addQueryItem("key", m_apiKey);
    requestUrl.setQuery(query);

    QNetworkRequest request(requestUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // --- Construction du payload JSON pour Google Gemini ---
    QJsonObject payload;
    
    // 1. System Instruction (Native support in Gemini 1.5)
    // On combine m_systemPrompt et ancientMemory
    QString fullSystemPrompt = m_systemPrompt;
    if (!ancientMemory.isEmpty()) {
        if (!fullSystemPrompt.isEmpty()) fullSystemPrompt += "\n\n";
        fullSystemPrompt += "--- LONG-TERM MEMORY SUMMARY ---\n" + ancientMemory;
    }

    if (!fullSystemPrompt.isEmpty()) {
        QJsonObject systemInstruction;
        QJsonObject parts;
        parts["text"] = fullSystemPrompt;
        systemInstruction["parts"] = QJsonObject{{"text", fullSystemPrompt}};
        // Note: "parts" doit être un tableau ou un objet selon la version, mais v1beta attend souvent un tableau "parts": [{text: ...}]
        // Correction: system_instruction: { parts: [ { text: "..." } ] }
        systemInstruction["parts"] = QJsonArray{ QJsonObject{{"text", fullSystemPrompt}} };
        
        payload["system_instruction"] = systemInstruction;
    }

    QJsonArray contentsArray;

    for (int i = 0; i < history.size(); ++i) {
        const ChatMessage &msg = history[i];
        QJsonObject turn;

        // Le rôle de l'IA est "model" chez Google
        turn["role"] = msg.isLocalMessage() ? "user" : "model";

        QJsonArray partsArray;
        
        // Texte du message
        if (!msg.text().isEmpty()) {
            partsArray.append(QJsonObject{{"text", msg.text()}});
        }
        
        // Si c'est le dernier message (le nôtre) et qu'il y a des fichiers
        if (i == history.size() - 1 && msg.isLocalMessage() && !attachmentFileIds.isEmpty()) {
             // Déduplication simple
            QSet<QString> seen;
            for (const QString &fid : attachmentFileIds) {
                if (fid.isEmpty() || seen.contains(fid)) continue;
                seen.insert(fid);
                
                // fid est "uri|mimeType"
                QStringList parts = fid.split('|');
                if (parts.size() == 2) {
                    QString uri = parts[0];
                    QString mimeType = parts[1];
                    
                    QJsonObject fileData;
                    fileData["mime_type"] = mimeType;
                    fileData["file_uri"] = uri;
                    
                    partsArray.append(QJsonObject{{"file_data", fileData}});
                }
            }
        }

        turn["parts"] = partsArray;
        contentsArray.append(turn);
    }

    payload["contents"] = contentsArray;

    QByteArray data = QJsonDocument(payload).toJson();
    QNetworkReply *reply = m_manager->post(request, data);

    connect(reply, &QNetworkReply::finished, this, [this, reply, kind]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument jsonResponse = QJsonDocument::fromJson(reply->readAll());
            if (jsonResponse.isNull() || !jsonResponse.isObject()) {
                emit errorOccurred("Invalid JSON response from Google API.");
                reply->deleteLater();
                return;
            }

            QJsonObject geminiResponse = jsonResponse.object();

            // --- PARSING de la réponse Gemini et création de InterlocutorReply ---
            InterlocutorReply cleanReply;
            cleanReply.kind = kind; // On propage le 'kind'

            // Extraire le texte de la réponse
            if (geminiResponse.contains("candidates") && geminiResponse["candidates"].isArray()) {
                QJsonArray candidates = geminiResponse["candidates"].toArray();
                if (!candidates.isEmpty()) {
                    // On navigue dans la structure spécifique à Gemini
                    QJsonArray parts = candidates[0].toObject()["content"].toObject()["parts"].toArray();
                    for(const QJsonValue &part : parts) {
                        cleanReply.text += part.toObject()["text"].toString();
                    }
                }
            }

            // Extraire l'usage des tokens
            if (geminiResponse.contains("usageMetadata")) {
                QJsonObject usage = geminiResponse["usageMetadata"].toObject();
                cleanReply.inputTokens = usage["promptTokenCount"].toInt();
                cleanReply.outputTokens = usage["candidatesTokenCount"].toInt();
                cleanReply.totalTokens = usage["totalTokenCount"].toInt();
            }

            emit replyReady(cleanReply);

        } else {
            emit errorOccurred("Google API Error: " + reply->errorString() + " | Body: " + reply->readAll());
        }
        reply->deleteLater();
    });
}

void GoogleAIInterlocutor::uploadFile(QString fileName, const QByteArray &content, const QString &purpose)
{
    // Gemini API Upload URL
    QUrl url("https://generativelanguage.googleapis.com/upload/v1beta/files");
    QUrlQuery query;
    query.addQueryItem("key", m_apiKey);
    url.setQuery(query);

    // Détection du type MIME
    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFileNameAndData(fileName, content);
    QString mimeType = mime.name();

    // Construction de la requête Multipart
    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::RelatedType);

    // Partie Métadonnées (JSON)
    QHttpPart metadataPart;
    metadataPart.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=UTF-8");
    QJsonObject metadata;
    QJsonObject fileObj;
    fileObj["display_name"] = QFileInfo(fileName).fileName();
    metadata["file"] = fileObj;
    metadataPart.setBody(QJsonDocument(metadata).toJson());
    multiPart->append(metadataPart);

    // Partie Fichier
    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, mimeType);
    filePart.setBody(content);
    multiPart->append(filePart);

    QNetworkRequest request(url);
    // Header X-Goog-Upload-Protocol requis pour l'upload multipart
    request.setRawHeader("X-Goog-Upload-Protocol", "multipart");

    QNetworkReply *reply = m_manager->post(request, multiPart);
    multiPart->setParent(reply); // Le reply prend la propriété du multipart

    connect(reply, &QNetworkReply::finished, this, [this, reply, purpose, mimeType]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonObject obj = doc.object();
            if (obj.contains("file")) {
                QJsonObject fileData = obj["file"].toObject();
                QString uri = fileData["uri"].toString();
                
                // Hack: on encode le mimeType dans l'ID pour le récupérer dans sendRequest
                // Format: "uri|mimeType"
                QString compositeId = uri + "|" + mimeType;
                
                qDebug() << "Google File Uploaded:" << compositeId;
                emit fileUploaded(compositeId, purpose);
            } else {
                emit fileUploadFailed("Google Upload: No 'file' object in response.");
            }
        } else {
            emit fileUploadFailed("Google Upload Error: " + reply->errorString());
        }
        reply->deleteLater();
    });
}

void GoogleAIInterlocutor::deleteFile(const QString &fileId)
{
    // fileId est sous la forme "uri|mimeType" ou juste "uri"
    QString uri = fileId.split('|').first();
    
    QUrl url(uri);
    QUrlQuery query;
    query.addQueryItem("key", m_apiKey);
    url.setQuery(query);

    QNetworkRequest request(url);
    QNetworkReply *reply = m_manager->deleteResource(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, fileId]() {
        bool success = (reply->error() == QNetworkReply::NoError);
        if (!success) {
            qWarning() << "Google Delete Error:" << reply->errorString();
        }
        emit fileDeleted(fileId, success);
        reply->deleteLater();
    });
}
