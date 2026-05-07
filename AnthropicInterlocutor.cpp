// Begin source file AnthropicInterlocutor.cpp
#include "AnthropicInterlocutor.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

AnthropicInterlocutor::AnthropicInterlocutor(QString interlocutorName, const QString &apiKey,
                                             const QUrl &url, const QString &model,
                                             QObject *parent)
    : Interlocutor(interlocutorName, parent)
    , m_apiKey(apiKey)
    , m_url(url)
    , m_model(model)
    , m_nextNoteId(1)
{
    qDebug() << "Creating AnthropicInterlocutor url=" << url << "model=" << model;
    m_manager = new QNetworkAccessManager(this);
    loadNotes();
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

    // 1. System prompt (top-level "system" field in the Anthropic API).
    //    We combine: personality prompt + ancient memory + notes instructions.
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

    // Notes / scrapbook system (same as DeepSeekInterlocutor)
    QSettings settings("Tether", "ChatApp");
    if (settings.value("chat/deepSeekNotesEnabled", true).toBool())
    {
        if (!fullSystemPrompt.isEmpty())
            fullSystemPrompt += "\n\n";
        QString notesContent = getNotesString();
        if (notesContent.isEmpty())
        {
            notesContent = "(No notes currently saved. Feel free to add some by using NOTE{...}, "
                           "QUESTION{...} or IDEA{...}!)";
        }
        fullSystemPrompt +=
            "You are equipped with a personal notebook to act as your long-term memory and scratchpad. "
            "Whenever you include 'NOTE{...}', 'QUESTION{...}', or 'IDEA{...}' in your responses, the "
            "text inside the curly braces will be appended to your personal notes. Each note is "
            "assigned a unique ID. If you wish to delete a note, simply output 'DELETE{<ID>}' in your "
            "response. These notes are preserved across sessions and provided to you in every prompt. "
            "Notes are optional—you don't have to include one with every message—but you can use them "
            "to keep track of things you want to remember over the long term. "
            "Here is the current state of your personal notes:\n\n" +
            notesContent;
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

                // Process notes/ideas/questions/deletes embedded in the reply,
                // then strip the tags from the displayed text if needed.
                cleanReply.text = processNotesFromReply(cleanReply.text);

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

// ── Notes / scrapbook helpers ────────────────────────────────────────────────
// Implementation is identical in spirit to DeepSeekInterlocutor.

void AnthropicInterlocutor::loadNotes()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
                   "/TetherChats/" + m_interlocutorName + "_notes.md";
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QString content = file.readAll();
    file.close();

    QRegularExpression re("^(\\d+):\\s+(.*?)(?=\\n^\\d+:\\s+|\\z)",
                          QRegularExpression::MultilineOption |
                              QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator it = re.globalMatch(content);
    while (it.hasNext())
    {
        QRegularExpressionMatch match = it.next();
        int id      = match.captured(1).toInt();
        QString note = match.captured(2).trimmed();
        m_notes[id] = note;
        if (id >= m_nextNoteId)
            m_nextNoteId = id + 1;
    }
}

void AnthropicInterlocutor::saveNotes()
{
    QString dirPath =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/TetherChats";
    QDir dir;
    if (!dir.exists(dirPath))
        dir.mkpath(dirPath);

    QString path = dirPath + "/" + m_interlocutorName + "_notes.md";
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        qWarning() << "AnthropicInterlocutor: Could not open notes file for writing:" << path;
        return;
    }
    QTextStream out(&file);
    for (auto it = m_notes.begin(); it != m_notes.end(); ++it)
        out << it.key() << ": " << it.value() << "\n";
    file.close();
}

QString AnthropicInterlocutor::getNotesString() const
{
    QString result;
    for (auto it = m_notes.begin(); it != m_notes.end(); ++it)
        result += QString::number(it.key()) + ": " + it.value() + "\n";
    return result;
}

QString AnthropicInterlocutor::processNotesFromReply(const QString &replyText)
{
    bool notesChanged = false;

    // Process additions: NOTE{...}, IDEA{...}, QUESTION{...}
    QRegularExpression addRe("(?:NOTE|QUESTION|IDEA)\\{(.*?)\\}",
                             QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator addIt = addRe.globalMatch(replyText);
    while (addIt.hasNext())
    {
        QRegularExpressionMatch match = addIt.next();
        QString noteContent = match.captured(0).trimmed(); // stores the full 'NOTE{xyz}' string
        if (!noteContent.isEmpty())
        {
            m_notes[m_nextNoteId++] = noteContent;
            notesChanged = true;
        }
    }

    // Process deletions: DELETE{id}
    QRegularExpression delRe("DELETE\\{(\\d+)\\}");
    QRegularExpressionMatchIterator delIt = delRe.globalMatch(replyText);
    while (delIt.hasNext())
    {
        QRegularExpressionMatch match = delIt.next();
        int id = match.captured(1).toInt();
        if (m_notes.contains(id))
        {
            m_notes.remove(id);
            notesChanged = true;
        }
    }

    if (notesChanged)
        saveNotes();

    // Strip note/delete tags from displayed text when the user opted out of seeing them
    QSettings settings("Tether", "ChatApp");
    if (!settings.value("chat/displayNotesEnabled", true).toBool())
    {
        QString cleanedText = replyText;
        cleanedText.remove(addRe);
        cleanedText.remove(delRe);
        return cleanedText.trimmed();
    }

    return replyText;
}
// End source file AnthropicInterlocutor.cpp
