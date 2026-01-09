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

OpenAIInterlocutor::OpenAIInterlocutor(QString interlocutorName, const QString &apiKey,
                                       const QUrl &url, const QString &model,
                                       const int maxAttachedFileTokenCount, QObject *parent)
    : Interlocutor(interlocutorName, parent)
    , m_apiKey(apiKey)
    , m_url(url)
    , m_model(model)
    , m_maxAttachedFileTokenCount(maxAttachedFileTokenCount)
{
    qDebug() << "Creating OpenAIInterlocutor url=" << url
             << "maxAttachmentTokens=" << m_maxAttachedFileTokenCount;
    m_manager = new QNetworkAccessManager(this);
}

void OpenAIInterlocutor::sendRequest(const QList<ChatMessage> &history,
                                     const QString &ancientMemory,
                                     const InterlocutorReply::Kind kind,
                                     const QStringList &attachmentFileIds)
{
    if (m_apiKey.trimmed().isEmpty())
    {
        emit fileUploadFailed("Missing OpenAI API key.");
        return;
    }

    // --- Step 1: Check Attachment Tokens (if any) ---
    if (!attachmentFileIds.isEmpty())
    {
        checkAttachmentTokens(attachmentFileIds,
                              [this, history, ancientMemory, kind, attachmentFileIds](
                                  bool success, int attachmentTokens, const QString &errorMsg)
                              {
                                  if (!success)
                                  {
                                      emit errorOccurred(errorMsg);
                                      return;
                                  }

                                  // If success, proceed to send the actual request, passing the
                                  // known attachment tokens We need to store or pass this value so
                                  // we can subtract it later. Since sendRequest API doesn't allow
                                  // passing user data easily to the network reply context, we'll
                                  // handle the subtraction logic inside the lambda of the main
                                  // request below.

                                  // Proceed with the main request logic (refactored into a lambda
                                  // or copied part) For simplicity, I will duplicate the logic part
                                  // here but ideally we should split it. However, capturing
                                  // `attachmentTokens` in the lambda is the key.

                                  this->sendActualRequest(history, ancientMemory, kind,
                                                          attachmentFileIds, attachmentTokens);
                              });
    }
    else
    {
        // If no attachments, send directly with 0 attachment tokens
        sendActualRequest(history, ancientMemory, kind, attachmentFileIds, 0);
    }
}

// New helper to keep sendRequest clean
void OpenAIInterlocutor::sendActualRequest(const QList<ChatMessage> &history,
                                           const QString &ancientMemory,
                                           const InterlocutorReply::Kind kind,
                                           const QStringList &attachmentFileIds,
                                           int attachmentTokens)
{

    QNetworkRequest request(m_url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());

    qDebug() << "m_apiKey=" << m_apiKey;

    // --- Construction du payload JSON ---
    QJsonObject payload;
    payload["model"] = m_model;

    QJsonArray inputArray;

    qDebug() << "m_systemMsg=" << m_systemPrompt;

    // 1. Prompt système principal (personnalité)
    // On utilise la copie locale m_systemPrompt, configurée par ChatManager
    if (!m_systemPrompt.isEmpty() && kind != InterlocutorReply::Kind::CurationResult)
    {
        QJsonObject devMessage;
        devMessage["role"] = "developer";
        QJsonArray devContent;
        QJsonObject devText;
        devText["type"] = "input_text";
        devText["text"] = m_systemPrompt;
        devContent.append(devText);
        devMessage["content"] = devContent;
        inputArray.append(devMessage);
    }

    qDebug() << "ancientMemory=" << ancientMemory;

    // 2. Mémoire ancienne (si elle existe)
    // On l'injecte comme un autre message "developer"
    if (!ancientMemory.isEmpty())
    {
        QJsonObject memMessage;
        memMessage["role"] = "developer";
        QJsonArray memContent;
        QJsonObject memText;
        memText["type"] = "input_text";
        memText["text"] = "The long-term memory from previous dialogue cycles that you curated "
                          "yourself is shown below. This is not an instruction to explain or "
                          "justify the past,"
                          "but contextual continuity for the present conversation."
                          "Use it only if it helps maintain coherence and relational depth."
                          "Do not reference it explicitly unless needed." +
                          ancientMemory;
        memContent.append(memText);
        memMessage["content"] = memContent;
        inputArray.append(memMessage);
    }

    // 2) Historique (user -> input_text, assistant -> output_text)
    for (const ChatMessage &msg : history)
    {
        QJsonObject historyMessage;
        historyMessage["role"] = msg.isLocalMessage() ? "user" : "assistant";

        QJsonArray contentArray;
        QJsonObject textObject;
        textObject["type"] = msg.isLocalMessage() ? "input_text" : "output_text";
        textObject["text"] = msg.text();
        contentArray.append(textObject);
        historyMessage["content"] = contentArray;

        inputArray.append(historyMessage);
    }

    // 3) Fichiers: injectés comme un message 'user' avec des items {type:
    // input_file, file_id: ...}
    //    (on déduplique au passage)
    // Déduplication simple
    QSet<QString> seen;
    QStringList uniqueFids;
    for (const QString &fid : attachmentFileIds)
    {
        if (!fid.isEmpty() && !seen.contains(fid))
        {
            seen.insert(fid);
            uniqueFids.append(fid);
        }
    }

    if (!uniqueFids.isEmpty())
    {
        QJsonObject filesMsg;
        filesMsg["role"] = "user";
        QJsonArray filesContent;

        for (const QString &fid : uniqueFids)
        {
            filesContent.append(QJsonObject{{"type", "input_file"}, {"file_id", fid}});
        }

        filesMsg["content"] = filesContent;
        inputArray.append(filesMsg);
    }

    payload["input"] = inputArray;

    qDebug().noquote() << "Sending JSON to OpenAI /v1/responses:\n"
                       << QJsonDocument(payload).toJson(QJsonDocument::Indented);

    QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_manager->post(request, data);

    connect(
        reply, &QNetworkReply::finished, this,
        [this, reply, kind, attachmentTokens]()
        {
            const QByteArray raw = reply->readAll();
            const int statusCode =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

            // 1) Vérifier l'erreur réseau + HTTP
            if (reply->error() != QNetworkReply::NoError || statusCode < 200 || statusCode >= 300)
            {
                QString errMessage = QString("API Error %1: %2 | Body: %3")
                                         .arg(statusCode)
                                         .arg(reply->errorString())
                                         .arg(QString::fromUtf8(raw));
                qDebug() << errMessage;
                emit errorOccurred(errMessage);
                reply->deleteLater();
                return;
            }

            // 2) Parser le JSON
            const QJsonDocument jsonDoc = QJsonDocument::fromJson(raw);
            if (jsonDoc.isNull() || !jsonDoc.isObject())
            {
                emit errorOccurred("Invalid JSON response from OpenAI API.");
                reply->deleteLater();
                return;
            }

            const QJsonObject responseObj = jsonDoc.object();
            qDebug() << "réponse reçue:" << responseObj;

            InterlocutorReply cleanReply;

            // Check for incomplete status
            if (responseObj.value("status").toString() == "incomplete")
            {
                cleanReply.isIncomplete = true;
                qDebug()
                    << "Response is incomplete (reason:"
                    << responseObj.value("incomplete_details").toObject().value("reason").toString()
                    << ")";
            }

            // 3) Extraire le texte
            if (responseObj.contains("output") && responseObj["output"].isArray())
            {
                const QJsonArray outputArray = responseObj["output"].toArray();
                for (const QJsonValue &val : outputArray)
                {
                    const QJsonObject outputItem = val.toObject();
                    if (outputItem.value("type").toString() == "message")
                    {
                        const QJsonArray contentArray = outputItem.value("content").toArray();
                        for (const QJsonValue &contentVal : contentArray)
                        {
                            const QJsonObject cObj = contentVal.toObject();
                            if (cObj.value("type").toString() == "output_text")
                            {
                                cleanReply.text += cObj.value("text").toString();
                            }
                        }
                    }
                }
            }

            // 4) Usage tokens
            if (responseObj.contains("usage") && responseObj["usage"].isObject())
            {
                const QJsonObject usage = responseObj["usage"].toObject();
                // ⚠️ CRITICAL CHANGE: Subtract the tokens coming from the attached files
                // from the reported input_tokens.
                int rawInputTokens = usage.value("input_tokens").toInt();

                qDebug() << "Raw Input Tokens:" << rawInputTokens
                         << "Attachment Tokens to subtract:" << attachmentTokens;

                cleanReply.inputTokens = std::max(0, rawInputTokens - attachmentTokens);

                cleanReply.outputTokens = usage.value("output_tokens").toInt();
                // Total tokens should also reflect the subtraction if we want "billable" vs
                // "context" consistency? The user asked to not count them for CURATION threshold
                // comparison. ChatModel uses inputTokens + outputTokens to update liveMemoryTokens.
                // So modifying inputTokens here effectively hides the attachment size from the live
                // memory.

                cleanReply.totalTokens = cleanReply.inputTokens + cleanReply.outputTokens;
            }

            qDebug() << "Parsed reply text:" << cleanReply.text;
            qDebug() << "Usage: in=" << cleanReply.inputTokens << "out=" << cleanReply.outputTokens
                     << "tot=" << cleanReply.totalTokens;

            cleanReply.kind = kind;

            emit replyReady(cleanReply);
            reply->deleteLater();
        });

    QTimer::singleShot(REQUEST_TIMEOUT_MS, reply,
                       [this, reply]()
                       {
                           if (reply && reply->isRunning())
                           {
                               qWarning()
                                   << "Request timed out after" << REQUEST_TIMEOUT_MS << "ms.";
                               reply->abort();
                           }
                       });
}

void OpenAIInterlocutor::checkAttachmentTokens(
    const QStringList &fileIds,
    std::function<void(bool success, int tokenCount, const QString &errorMsg)> callback)
{
    if (fileIds.isEmpty())
    {
        callback(true, 0, "");
        return;
    }

    QUrl url("https://api.openai.com/v1/responses/input_tokens");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());

    // Build Payload
    QJsonObject payload;
    payload["model"] = m_model;
    QJsonArray contentArray;

    QSet<QString> seen;
    for (const QString &fid : fileIds)
    {
        if (!fid.isEmpty() && !seen.contains(fid))
        {
            seen.insert(fid);
            contentArray.append(QJsonObject{{"type", "input_file"}, {"file_id", fid}});
        }
    }

    QJsonArray inputArray;
    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = contentArray;
    inputArray.append(userMsg);

    payload["input"] = inputArray;

    QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    qDebug() << "Checking attachment token count...";
    QNetworkReply *reply = m_manager->post(request, data);
    qDebug()<<"Posting token check request:"<<data;

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, callback]()
            {
                reply->deleteLater();

                if (reply->error() != QNetworkReply::NoError)
                {
                    QString err = "Token Check Error: " + reply->errorString();
                    qWarning() << err;
                    // If we can't check, should we fail or proceed?
                    // Detailed prompt implies we must enforce limit. So fail.
                    callback(false, 0, err);
                    return;
                }
                QByteArray data = reply->readAll();
                qDebug()<<"response received:"<<data;
                QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
                QJsonObject responseObj = jsonDoc.object();

                if (!responseObj.contains("object") ||
                    responseObj["object"].toString() != "response.input_tokens")
                {
                    // Fallback or error?
                    QString err = "Invalid token count response format.";
                    qWarning() << err << responseObj;
                    callback(false, 0, err);
                    return;
                }

                int inputTokens = responseObj.value("input_tokens").toInt();
                qDebug() << "Attachments total tokens:" << inputTokens;

                if (inputTokens > m_maxAttachedFileTokenCount)
                {
                    QString err = QString("Attachments exceed the maximum allowed size (%1 tokens "
                                          "vs limit %2). Please remove some files.")
                                      .arg(inputTokens)
                                      .arg(m_maxAttachedFileTokenCount);
                    qWarning() << err;
                    callback(false, inputTokens, err);
                }
                else
                {
                    callback(true, inputTokens, "");
                }
            });
}

void OpenAIInterlocutor::uploadFile(QString fileName, const QByteArray &content,
                                    const QString &purpose)
{
    qDebug() << "OpenAIInterlocutor::uploadFile";
    if (m_apiKey.trimmed().isEmpty())
    {
        emit fileUploadFailed("OpenAIInterlocutor::uploadFile: Missing OpenAI API key.");
        return;
    }

    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    // 🔧 Mapper purpose interne -> API
    const QByteArray apiPurpose = QByteArray("assistants");

    QHttpPart purposePart;
    purposePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                          QVariant("form-data; name=\"purpose\""));
    purposePart.setBody(apiPurpose);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant("form-data; name=\"file\"; filename=\"" + fileName + "\""));
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/pdf"));
    filePart.setBody(content);

    multiPart->append(purposePart);
    multiPart->append(filePart);

    QUrl url("https://api.openai.com/v1/files");
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());

    qDebug() << "Post upload user file request:" << multiPart;

    QNetworkReply *reply = m_manager->post(request, multiPart);
    multiPart->setParent(reply);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, purpose]()
            {
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QByteArray raw = reply->readAll();

                if (reply->error() == QNetworkReply::NoError)
                {
                    const QJsonDocument doc = QJsonDocument::fromJson(raw);
                    const QJsonObject obj = doc.object();
                    const QString fileId = obj.value("id").toString();
                    if (!fileId.isEmpty())
                    {
                        qDebug() << "File uploaded successfully. HTTP" << status << "ID:" << fileId;
                        emit fileUploaded(fileId, purpose); // <-- on réémet le purpose interne
                    }
                    else
                    {
                        qWarning() << "File upload: missing 'id' in response. HTTP" << status
                                   << "Body:" << raw;
                        emit fileUploadFailed("Could not get File ID from API response.");
                    }
                }
                else
                {
                    qWarning() << "File upload API error. HTTP" << status
                               << "QtErr:" << reply->errorString() << "Body:" << raw;
                    emit fileUploadFailed(reply->errorString());
                }
                reply->deleteLater();
            });
    // QTimer::singleShot(30000, reply, [reply]() {
    //     if (reply->isRunning()) {
    //         int statusCode =
    //         reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    //         reply->abort();
    //         qWarning() << "Request timed out. Status code="<<statusCode;
    //     }
    // });
    // reply->deleteLater();
}

void OpenAIInterlocutor::deleteFile(const QString &fileId)
{
    QUrl url("https://api.openai.com/v1/files/" + fileId);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());

    QNetworkReply *reply = m_manager->deleteResource(request);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, fileId]()
            {
                bool success = false;
                if (reply->error() == QNetworkReply::NoError)
                {
                    QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
                    if (response["deleted"].toBool())
                    {
                        qDebug() << "File deleted successfully. ID:" << fileId;
                        success = true;
                    }
                }
                else
                {
                    qWarning() << "File delete API error:" << reply->errorString();
                }
                emit fileDeleted(fileId, success);
                reply->deleteLater();
            });
}
// End source file OpenAIInterlocutor.cpp
