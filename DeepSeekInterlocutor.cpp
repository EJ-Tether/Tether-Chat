#include "DeepSeekInterlocutor.h"
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

DeepSeekInterlocutor::DeepSeekInterlocutor(QString interlocutorName, const QString &apiKey,
                                           const QUrl &url, const QString &model, QObject *parent)
    : Interlocutor(interlocutorName, parent)
    , m_apiKey(apiKey)
    , m_url(url)
    , m_model(model)
{
    m_manager = new QNetworkAccessManager(this);
}

void DeepSeekInterlocutor::sendRequest(const QList<ChatMessage> &history,
                                       const QString &ancientMemory,
                                       const InterlocutorReply::Kind kind,
                                       const QStringList &attachmentFileIds)
{
    // DeepSeek API doesn't support file attachments in the standard chat completions endpoint
    // in the same way as the custom OpenAI implementation. We ignore attachmentFileIds.
    if (!attachmentFileIds.isEmpty())
    {
        qWarning() << "DeepSeekInterlocutor: Attachments are not supported and will be ignored.";
    }

    QNetworkRequest request(m_url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());

    QJsonObject payload;
    payload["model"] = m_model;
    payload["stream"] = false;

    QJsonArray messages;

    // 1. System Prompt
    if (!m_systemPrompt.isEmpty())
    {
        QJsonObject systemMsg;
        systemMsg["role"] = "system";
        systemMsg["content"] = m_systemPrompt;
        messages.append(systemMsg);
    }

    // 2. Ancient Memory
    if (!ancientMemory.isEmpty())
    {
        QJsonObject memoryMsg;
        memoryMsg["role"] = "system";
        // We present ancient memory as a system instruction for context
        memoryMsg["content"] = "The long-term memory from previous dialogue cycles is shown below. "
                               "Use it for context continuity only:\n" +
                               ancientMemory;
        messages.append(memoryMsg);
    }

    // 3. Chat History
    for (const ChatMessage &msg : history)
    {
        QJsonObject chatMsg;
        chatMsg["role"] = msg.isLocalMessage() ? "user" : "assistant";
        chatMsg["content"] = msg.text();
        messages.append(chatMsg);
    }

    payload["messages"] = messages;

    QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_manager->post(request, data);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, kind]()
            {
                const QByteArray raw = reply->readAll();
                const int statusCode =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (reply->error() != QNetworkReply::NoError || statusCode < 200 ||
                    statusCode >= 300)
                {
                    QString errMessage = QString("DeepSeek API Error %1: %2 | Body: %3")
                                             .arg(statusCode)
                                             .arg(reply->errorString())
                                             .arg(QString::fromUtf8(raw));
                    qWarning() << errMessage;
                    emit errorOccurred(errMessage);
                    reply->deleteLater();
                    return;
                }

                const QJsonDocument jsonDoc = QJsonDocument::fromJson(raw);
                if (jsonDoc.isNull() || !jsonDoc.isObject())
                {
                    emit errorOccurred("Invalid JSON response from DeepSeek API.");
                    reply->deleteLater();
                    return;
                }

                const QJsonObject responseObj = jsonDoc.object();
                InterlocutorReply cleanReply;
                cleanReply.kind = kind;

                // Parse standard OpenAI chat completion response
                if (responseObj.contains("choices") && responseObj["choices"].isArray())
                {
                    QJsonArray choices = responseObj["choices"].toArray();
                    if (!choices.isEmpty())
                    {
                        QJsonObject choice = choices[0].toObject();
                        QJsonObject message = choice["message"].toObject();
                        cleanReply.text = message["content"].toString();
                    }
                }

                // Parse usage
                if (responseObj.contains("usage") && responseObj["usage"].isObject())
                {
                    QJsonObject usage = responseObj["usage"].toObject();
                    cleanReply.inputTokens = usage["prompt_tokens"].toInt();
                    cleanReply.outputTokens = usage["completion_tokens"].toInt();
                    cleanReply.totalTokens = usage["total_tokens"].toInt();
                }

                emit replyReady(cleanReply);
                reply->deleteLater();
            });

    QTimer::singleShot(REQUEST_TIMEOUT_MS, reply,
                       [reply]()
                       {
                           if (reply && reply->isRunning())
                           {
                               reply->abort();
                           }
                       });
}

void DeepSeekInterlocutor::uploadFile(QString fileName, const QByteArray &content,
                                      const QString &purpose)
{
    Q_UNUSED(fileName);
    Q_UNUSED(content);
    Q_UNUSED(purpose);
    // Be honest: verify if DeepSeek supports files. Standard chat completion usually doesn't.
    emit fileUploadFailed("File upload is not supported by DeepSeekInterlocutor.");
}

void DeepSeekInterlocutor::deleteFile(const QString &fileId)
{
    Q_UNUSED(fileId);
    // Nothing to delete locally or remotely since we don't upload
    emit fileDeleted(fileId, false);
}
