#include "DeepSeekInterlocutor.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QSettings>


DeepSeekInterlocutor::DeepSeekInterlocutor(QString interlocutorName, const QString &apiKey,
                                           const QUrl &url, const QString &model, QObject *parent)
    : Interlocutor(interlocutorName, parent)
    , m_apiKey(apiKey)
    , m_url(url)
    , m_model(model)
    , m_nextNoteId(1)
{
    m_manager = new QNetworkAccessManager(this);
    loadNotes();
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

    // 3. Notes System
    QSettings settings("Tether", "ChatApp");
    if (settings.value("chat/deepSeekNotesEnabled", true).toBool())
    {
        QJsonObject notesMsg;
        notesMsg["role"] = "system";
        QString notesContent = getNotesString();
        if (notesContent.isEmpty())
        {
            notesContent = "(No notes currently saved. Feel free to add some by using NOTE{...}, "
                           "QUESTION{...} or IDEA{...}!)";
        }
        notesMsg["content"] =
            "You are equipped with a personal notebook to act as your long-term memory and scratchpad. "
            "Whenever you include 'NOTE{...}', 'QUESTION{...}', or 'IDEA{...}' in your responses, the "
            "text inside the curly braces will be appended to your personal notes. Each note is "
            "assigned a unique ID. If you wish to delete a note, simply output 'DELETE{<ID>}' in your "
            "response. These notes are preserved across sessions and provided to you in every prompt. "
            "Notes are optional—you don't have to include one with every message —but you can use them "
            "to keep track of things you want to remember over the long term. "
            "Here is the current state of your personal notes:\n\n" +
            notesContent;
        messages.append(notesMsg);
    }

    // 4. Chat History
    for (const ChatMessage &msg : history)
    {
        if (msg.isError() || msg.isTypingIndicator) continue;

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

                        // Process any notes/ideas/questions/deletes generated in the reply
                        // Returns the text with note/delete tags stripped out
                        cleanReply.text = processNotesFromReply(cleanReply.text);
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

void DeepSeekInterlocutor::loadNotes()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
                   "/TetherChats/" + m_interlocutorName + "_notes.md";
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return;
    }
    QString content = file.readAll();
    file.close();

    QRegularExpression re("^(\\d+):\\s+(.*?)(?=\\n^\\d+:\\s+|\\z)",
                          QRegularExpression::MultilineOption |
                              QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator it = re.globalMatch(content);
    while (it.hasNext())
    {
        QRegularExpressionMatch match = it.next();
        int id = match.captured(1).toInt();
        QString note = match.captured(2).trimmed();
        m_notes[id] = note;
        if (id >= m_nextNoteId)
        {
            m_nextNoteId = id + 1;
        }
    }
}

void DeepSeekInterlocutor::saveNotes()
{
    QString dirPath =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/TetherChats";
    QDir dir;
    if (!dir.exists(dirPath))
    {
        dir.mkpath(dirPath);
    }

    QString path = dirPath + "/" + m_interlocutorName + "_notes.md";
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        qWarning() << "Could not open notes file for writing:" << path;
        return;
    }
    QTextStream out(&file);
    for (auto it = m_notes.begin(); it != m_notes.end(); ++it)
    {
        out << it.key() << ": " << it.value() << "\n";
    }
    file.close();
}

QString DeepSeekInterlocutor::getNotesString() const
{
    QString result;
    for (auto it = m_notes.begin(); it != m_notes.end(); ++it)
    {
        result += QString::number(it.key()) + ": " + it.value() + "\n";
    }
    return result;
}

QString DeepSeekInterlocutor::processNotesFromReply(const QString &replyText)
{
    bool notesChanged = false;

    // Process Additions: NOTE{...}, IDEA{...}, QUESTION{...}
    QRegularExpression addRe("(?:NOTE|QUESTION|IDEA)\\{(.*?)\\}",
                             QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator addIt = addRe.globalMatch(replyText);
    while (addIt.hasNext())
    {
        QRegularExpressionMatch match = addIt.next();
        QString content = match.captured(0).trimmed(); // Storing the full 'NOTE{xyz}' string
        if (!content.isEmpty())
        {
            m_notes[m_nextNoteId++] = content;
            notesChanged = true;
        }
    }

    // Process Deletions: DELETE{id}
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
    {
        saveNotes();
    }

    // Strip note/delete tags from the displayed text only when the user has
    // chosen not to display them (chat/displayNotesEnabled == false).
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
