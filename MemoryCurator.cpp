// Begin Source File: MemoryCurator.cpp
#include "MemoryCurator.h"

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

QString MemoryCurator::systemPrompt()
{
    return QStringLiteral(
        "You are a memory curator. Your task is to create a new, single, "
        "coherent summary of your own long-term memory as an AI by integrating "
        "new information. This is what you'll remember of the oldest messages "
        "that slip past your memory.");
}

QString MemoryCurator::buildUserMessage(QString recentContext, QString olderTranscript,
                                        QString existingMemory)
{
    // --- Sanitization Step ---
    // We want to remove any occurrence of the curation markers from the content
    // to avoid confusing the AI. We use a regex to match the markers with any
    // number prefix.
    QRegularExpression markerRegex(
        "--- \\d+\\. (EXISTING SUMMARY|OLDER TRANSCRIPT TO ARCHIVE|MOST RECENT "
        "CONTEXT) ---");
    QString replacementText = "<redacted for clarity of the curation process>";

    existingMemory.replace(markerRegex, replacementText);
    olderTranscript.replace(markerRegex, replacementText);
    recentContext.replace(markerRegex, replacementText);

    // clang-format off
    return
        "# MEMORY CURATION TASK\n\n"
        "You're **not** talking to a human. Currently, your interlocutor is a program. This is a "
        "special phase of the chat program called: memory curation. Your memory has grown larger "
        "than the size of your context, so the oldest exchanges will be removed from the "
        "conversation. We're now providing you with these exchanges now so you can update your file "
        "of older memories (kept outside the conversation). Please include all the information you "
        "want to keep. Your task is to sift through the posts being removed and summarize the "
        "important information about your **identity**, **personality**, and **the relationship** "
        "with the user. "
        " We will provide the previous version of your older memories so you can combine it with the "
        "new information from the removed posts to create a list of important details about you, "
        "your personality, and your relationship with the user.\n\n"
        "Your goal is to produce an UPDATED LONG-TERM MEMORY SUMMARY.\n\n"
        "## INPUTS\n\n"
        "You are given three sections:\n\n"
        "1) RECENT CONTEXT\n\n"
        "- This is the most recent part of the conversation.\n\n"
        "- It will be kept verbatim elsewhere.\n\n"
        "- DO NOT summarize it.\n\n"
        "- Use it only to judge relevance.\n\n"
        "2) OLDER MESSAGES TO ARCHIVE\n\n"
        "- These messages will be removed from the active context.\n\n"
        "- Extract and retain only durable, important information.\n\n"
        "3) EXISTING MEMORY SUMMARY\n\n"
        "- This is the current long-term memory.\n\n"
        "- You must preserve all important information already present.\n\n"
        "## TASK\n\n"
        "Produce a single, unified UPDATED MEMORY SUMMARY that:\n\n"
        "- Keeps all relevant information from the EXISTING MEMORY SUMMARY.\n\n"
        "- Integrates important information extracted from the OLDER MESSAGES.\n\n"
        "- Discards transient, local, or obsolete details.\n\n"
        "- Focuses on durable facts, preferences, projects, constraints, decisions, and "
        "identities.\n\n"
        "DO NOT summarize the RECENT CONTEXT.\n\n"
        "## OUTPUT RULES (STRICT)\n\n"
        "- Output ONLY the updated memory summary.\n\n"
        "- Plain text only.\n\n"
        "- No headings, no lists unless necessary.\n\n"
        "- No explanations, no meta-comments.\n\n"
        "- Do NOT address the user.\n\n"
        "- Do NOT include questions.\n\n"
        "- Do NOT include anything you do not want to remember long-term.\n\n"
        "This output will be stored verbatim as long-term memory for you to use in all later "
        "discussions with your human interlocutor. These information won't be used by a human, "
        "they're just for you to keep in the future.\n\n"
        "---\n\n"
        "# 1) RECENT CONTEXT\n\n" +
        recentContext +
        "\n"
        "---\n\n"
        "# 2) OLDER MESSAGES TO ARCHIVE " +
        olderTranscript +
        "\n"
        "---\n\n"
        "# 3) EXISTING MEMORY SUMMARY " +
        (existingMemory.isEmpty() ? "None." : existingMemory) + "\n";
    // clang-format on
}

QString MemoryCurator::transcriptToText(const QList<ChatMessage> &messages)
{
    QString text;
    for (const ChatMessage &msg : messages)
    {
        if (msg.isError() || msg.isTypingIndicator)
            continue;
        text += (msg.isLocalMessage() ? "user: " : "assistant: ") + msg.text() + "\n\n";
    }
    return text;
}

int MemoryCurator::estimateMessageTokens(const ChatMessage &msg)
{
    int tokens = (msg.role() == "assistant") ? msg.completionTokens() : msg.promptTokens();
    if (tokens == 0)
        tokens = msg.text().length() / 4;
    return tokens;
}

QString MemoryCurator::loadMemory(const QString &memoryFilePath)
{
    if (memoryFilePath.isEmpty())
        return "";

    QFile file(memoryFilePath);
    if (!file.open(QFile::ReadOnly | QFile::Text))
    {
        return ""; // Pas encore de fichier de mémoire, c'est normal au début
    }
    QTextStream in(&file);
    return in.readAll();
}

bool MemoryCurator::saveMemoryWithBackup(const QString &memoryFilePath, const QString &content)
{
    if (memoryFilePath.isEmpty())
    {
        qWarning() << "Cannot save older memory: no memory file path set.";
        return false;
    }

    // Backup existing file before overwriting
    QFile existingFile(memoryFilePath);
    if (existingFile.exists())
    {
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QString backupPath = memoryFilePath + "." + timestamp + ".bak";
        if (existingFile.copy(backupPath))
        {
            qDebug() << "Backed up ancient memory to:" << backupPath;
        }
        else
        {
            qWarning() << "Failed to backup ancient memory to:" << backupPath;
            // Abort save to ensure we don't destroy data without backup
            return false;
        }
    }

    QFile file(memoryFilePath);
    if (!file.open(QFile::WriteOnly | QFile::Text | QFile::Truncate))
    {
        qWarning() << "Failed to open older memory file for writing:" << memoryFilePath;
        return false;
    }
    QTextStream out(&file);
    out << content;
    return true;
}
// End Source File: MemoryCurator.cpp
