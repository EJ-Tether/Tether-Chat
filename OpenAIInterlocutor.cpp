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
    qDebug()<<"Creating OpenAIInterlocutor url="<<url;
    m_manager = new QNetworkAccessManager(this);
}

void OpenAIInterlocutor::setSystemPrompt(const QString &systemPrompt)
{
    m_systemMsg = QJsonObject();
    if (systemPrompt.isEmpty())
        return;

    m_systemMsg["role"] = "developer";
    QJsonArray contentArray;
    contentArray.append(QJsonObject{
        {"type", "input_text"},
        {"text", systemPrompt}
    });
    m_systemMsg["content"] = contentArray;
}

void OpenAIInterlocutor::sendRequest(const QList<ChatMessage> &history,
                                     const QStringList &attachmentFileIds)
{
    if (m_apiKey.trimmed().isEmpty()) {
        emit fileUploadFailed("Missing OpenAI API key.");
        return;
    }

    QNetworkRequest request(m_url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
    // Inoffensif ici; requis si tu utilises des tools plus tard.
    request.setRawHeader("OpenAI-Beta", "assistants=v2");

    // --- Construction du payload JSON ---
    QJsonObject payload;
    payload["model"] = m_model;

    QJsonArray inputArray;

    // 1) Prompt système (préparé par setSystemPrompt: role=developer + content[input_text])
    if (!m_systemMsg.isEmpty()) {
        inputArray.append(m_systemMsg);
    }

    // 2) Historique (user -> input_text, assistant -> output_text)
    for (const ChatMessage &msg : history) {
        QJsonObject messageObj;
        const bool isUser = msg.isLocalMessage();
        messageObj["role"] = isUser ? "user" : "assistant";

        const char* t = isUser ? "input_text" : "output_text";
        QJsonArray contentArray;
        contentArray.append(QJsonObject{
            {"type", t},
            {"text", msg.text()}
        });

        messageObj["content"] = contentArray;
        inputArray.append(messageObj);
    }

    // 3) Fichiers: injectés comme un message 'user' avec des items {type: input_file, file_id: ...}
    //    (on déduplique au passage)
    QStringList allAttachments = attachmentFileIds;
    if (!m_ancientMemoryFileId.isEmpty())
        allAttachments.append(m_ancientMemoryFileId);

    // Déduplication simple
    QSet<QString> seen;
    QStringList uniqueFids;
    for (const QString &fid : allAttachments) {
        if (!fid.isEmpty() && !seen.contains(fid)) {
            seen.insert(fid);
            uniqueFids.append(fid);
        }
    }

    if (!uniqueFids.isEmpty()) {
        QJsonObject filesMsg;
        filesMsg["role"] = "user";  // les input_file doivent être côté "user"
        QJsonArray filesContent;

        for (const QString &fid : uniqueFids) {
            filesContent.append(QJsonObject{
                {"type", "input_file"},
                {"file_id", fid}
            });
        }

        // (Optionnel) Tu peux ajouter un petit rappel textuel dans le même message :
        // filesContent.append(QJsonObject{
        //     {"type", "input_text"},
        //     {"text", "Voici des fichiers de contexte (mémoire ancienne + pièces jointes)."}
        // });

        filesMsg["content"] = filesContent;
        inputArray.append(filesMsg);
    }

    payload["input"] = inputArray;

    // ❌ PAS de "attachments" top-level (provoque 400)
    // ❌ PAS de "tools" obligatoires pour input_file (tu pourras les remettre plus tard si besoin)

    qDebug().noquote() << "Sending JSON to OpenAI /v1/responses:\n"
                       << QJsonDocument(payload).toJson(QJsonDocument::Indented);

    QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_manager->post(request, data);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QByteArray raw = reply->readAll();
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument jsonResponse = QJsonDocument::fromJson(raw);
            if (!jsonResponse.isNull() && jsonResponse.isObject()) {
                emit responseReceived(jsonResponse.object());
            } else {
                qWarning() << "Invalid JSON response from OpenAI API:" << raw;
                emit errorOccurred("Invalid JSON response from OpenAI API.");
            }
        } else {
            qWarning() << "API Error:" << statusCode << reply->errorString()
            << "\nResponse body:" << raw;
            emit errorOccurred(QString("API Error %1: %2").arg(statusCode).arg(reply->errorString()));
        }
        reply->deleteLater();
    });
}

void OpenAIInterlocutor::uploadFile(const QByteArray &content, const QString &purpose)
{
    qDebug()<<"OpenAIInterlocutor::uploadFile";
    if (m_apiKey.trimmed().isEmpty()) {
        emit fileUploadFailed("OpenAIInterlocutor::uploadFile: Missing OpenAI API key.");
        return;
    }

    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    // 🔧 Mapper purpose interne -> API
    const QByteArray apiPurpose = QByteArray("assistants");

    QHttpPart purposePart;
    purposePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                          QVariant("form-data; name=\"purpose\""));
    purposePart.setBody(apiPurpose);  // <-- was: purpose.toUtf8()

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
    //request.setRawHeader("OpenAI-Beta", "assistants=v2");

    qDebug()<<"Post upload user file request:"<<multiPart;

    QNetworkReply *reply = m_manager->post(request, multiPart);
    multiPart->setParent(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, purpose]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();

        if (reply->error() == QNetworkReply::NoError) {
            const QJsonDocument doc = QJsonDocument::fromJson(raw);
            const QJsonObject obj = doc.object();
            const QString fileId = obj.value("id").toString();
            if (!fileId.isEmpty()) {
                qDebug() << "File uploaded successfully. HTTP" << status << "ID:" << fileId;
                emit fileUploaded(fileId, purpose); // <-- on réémet le purpose interne
            } else {
                qWarning() << "File upload: missing 'id' in response. HTTP" << status << "Body:" << raw;
                emit fileUploadFailed("Could not get File ID from API response.");
            }
        } else {
            qWarning() << "File upload API error. HTTP" << status
                       << "QtErr:" << reply->errorString()
                       << "Body:" << raw;
            emit fileUploadFailed(reply->errorString());
        }
        reply->deleteLater();
    });
    //QTimer::singleShot(30000, reply, [reply]() {
    //    if (reply->isRunning()) {
    //        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    //        reply->abort();
    //        qWarning() << "Request timed out. Status code="<<statusCode;
    //    }
    //});
    //reply->deleteLater();
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
