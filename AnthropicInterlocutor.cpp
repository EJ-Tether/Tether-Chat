// Begin source file AnthropicInterlocutor.cpp
#include "AnthropicInterlocutor.h"
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

AnthropicInterlocutor::AnthropicInterlocutor(QString interlocutorName, const QString &apiKey,
                                             const QUrl &url, const QString &model,
                                             QObject *parent)
    : Interlocutor(interlocutorName, parent)
    , m_apiKey(apiKey)
    , m_url(url)
    , m_model(model)
{
    qDebug() << "Creating AnthropicInterlocutor url=" << url << "model=" << model;
    m_manager = new QNetworkAccessManager(this);
}

void AnthropicInterlocutor::sendRequest(const QList<ChatMessage> &history,
                                        const QString &ancientMemory,
                                        const InterlocutorReply::Kind kind,
                                        const QStringList &attachmentFileIds)
{
    if (m_apiKey.trimmed().isEmpty())
    {
        emit errorOccurred("Missing Anthropic API key.");
        return;
    }

    // Anthropic doesn't support file attachments via the Messages API
    if (!attachmentFileIds.isEmpty())
    {
        qWarning() << "AnthropicInterlocutor: File attachments are not supported and will be ignored.";
    }

    // --- Build the HTTP request ---
    QNetworkRequest request(m_url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("x-api-key",          m_apiKey.toUtf8());
    request.setRawHeader("anthropic-version",  "2023-06-01");

    // --- Build the JSON payload ---
    QJsonObject payload;
    payload["model"]      = m_model;
    payload["max_tokens"] = MAX_OUTPUT_TOKENS;

    // 1. System prompt (top-level "system" field in the Anthropic API)
    //    We combine the personality prompt and the ancient memory into one string.
    QString fullSystemPrompt;
    if (!m_systemPrompt.isEmpty() && kind != InterlocutorReply::Kind::CurationResult)
    {
        fullSystemPrompt = m_systemPrompt;
    }
    if (!ancientMemory.isEmpty())
    {
        if (!fullSystemPrompt.isEmpty())
            fullSystemPrompt += "\n\n";
        fullSystemPrompt +=
            "The long-term memory from previous dialogue cycles that you curated "
            "yourself is shown below. This is not an instruction to explain or justify the past, "
            "but contextual continuity for the present conversation. "
            "Use it only if it helps maintain coherence and relational depth. "
            "Do not reference it explicitly unless needed.\n" +
            ancientMemory;
    }
    if (!fullSystemPrompt.isEmpty())
    {
        payload["system"] = fullSystemPrompt;
    }

    // 2. Messages array (user / assistant turns)
    //    The Anthropic API requires strictly alternating user/assistant roles.
    //    We filter out errors and typing indicators, then ensure the first message is "user".
    QJsonArray messages;
    for (const ChatMessage &msg : history)
    {
        if (msg.isError() || msg.isTypingIndicator)
            continue;

        QJsonObject chatMsg;
        chatMsg["role"]    = msg.isLocalMessage() ? "user" : "assistant";
        chatMsg["content"] = msg.text();
        messages.append(chatMsg);
    }

    // Guard: the Anthropic API rejects an empty messages array or one that does
    // not start with a "user" turn.
    if (messages.isEmpty() || messages.first().toObject()["role"].toString() != "user")
    {
        emit errorOccurred("AnthropicInterlocutor: Cannot send request — "
                           "message history is empty or does not start with a user message.");
        return;
    }

    payload["messages"] = messages;

    QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    // qDebug().noquote() << "Sending JSON to Anthropic:\n"
    //                    << QJsonDocument(payload).toJson(QJsonDocument::Indented);

    QNetworkReply *reply = m_manager->post(request, data);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, kind]()
            {
                const QByteArray raw = reply->readAll();
                const int statusCode =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                // 1) Network / HTTP error check
                if (reply->error() != QNetworkReply::NoError || statusCode < 200 || statusCode >= 300)
                {
                    QString errMessage = QString("Anthropic API Error %1: %2 | Body: %3")
                                             .arg(statusCode)
                                             .arg(reply->errorString())
                                             .arg(QString::fromUtf8(raw));
                    qWarning() << errMessage;
                    emit errorOccurred(errMessage);
                    reply->deleteLater();
                    return;
                }

                // 2) Parse JSON
                const QJsonDocument jsonDoc = QJsonDocument::fromJson(raw);
                if (jsonDoc.isNull() || !jsonDoc.isObject())
                {
                    emit errorOccurred("Invalid JSON response from Anthropic API.");
                    reply->deleteLater();
                    return;
                }

                const QJsonObject responseObj = jsonDoc.object();
                InterlocutorReply cleanReply;
                cleanReply.kind = kind;

                // 3) Extract response text from content[].text
                //    Anthropic returns: { "content": [ { "type": "text", "text": "..." } ] }
                if (responseObj.contains("content") && responseObj["content"].isArray())
                {
                    const QJsonArray contentArray = responseObj["content"].toArray();
                    for (const QJsonValue &val : contentArray)
                    {
                        const QJsonObject contentItem = val.toObject();
                        if (contentItem.value("type").toString() == "text")
                        {
                            cleanReply.text += contentItem.value("text").toString();
                        }
                    }
                }

                // 4) Check for stop_reason to detect incomplete responses
                //    "max_tokens" means the output was truncated
                const QString stopReason = responseObj.value("stop_reason").toString();
                if (stopReason == "max_tokens")
                {
                    cleanReply.isIncomplete = true;
                    qDebug() << "AnthropicInterlocutor: Response stopped at max_tokens (incomplete).";
                }

                // 5) Token usage
                if (responseObj.contains("usage") && responseObj["usage"].isObject())
                {
                    const QJsonObject usage = responseObj["usage"].toObject();
                    cleanReply.inputTokens  = usage.value("input_tokens").toInt();
                    cleanReply.outputTokens = usage.value("output_tokens").toInt();
                    cleanReply.totalTokens  = cleanReply.inputTokens + cleanReply.outputTokens;
                }

                qDebug() << "Anthropic Usage: in=" << cleanReply.inputTokens
                         << "out=" << cleanReply.outputTokens
                         << "tot=" << cleanReply.totalTokens;

                emit replyReady(cleanReply);
                reply->deleteLater();
            });

    QTimer::singleShot(REQUEST_TIMEOUT_MS, reply,
                       [this, reply]()
                       {
                           if (reply && reply->isRunning())
                           {
                               qWarning() << "AnthropicInterlocutor: Request timed out after"
                                          << REQUEST_TIMEOUT_MS << "ms.";
                               reply->abort();
                           }
                       });
}

void AnthropicInterlocutor::uploadFile(QString fileName, const QByteArray &content,
                                       const QString &purpose)
{
    Q_UNUSED(fileName);
    Q_UNUSED(content);
    Q_UNUSED(purpose);
    emit fileUploadFailed("File upload is not supported by AnthropicInterlocutor.");
}

void AnthropicInterlocutor::deleteFile(const QString &fileId)
{
    Q_UNUSED(fileId);
    emit fileDeleted(fileId, false);
}
// End source file AnthropicInterlocutor.cpp
