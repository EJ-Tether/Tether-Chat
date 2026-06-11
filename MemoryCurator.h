// Begin Source File MemoryCurator.h
#ifndef MEMORYCURATOR_H
#define MEMORYCURATOR_H

#include <QList>
#include <QString>

#include "ChatMessage.h"

// Shared helpers for the long-term memory curation process.
//
// The curation prompt, the memory file I/O (with timestamped backups) and the
// transcript formatting were originally private to ChatModel. They are
// factored out here so that DuoChatModel (AI ↔ AI conversations) can run the
// exact same curation cycle for each side of a dialogue without duplicating
// the logic. Both models keep their own asynchronous state machines; this
// class is stateless.
class MemoryCurator
{
public:
    // System prompt of the curation request. Note: callers pass it through the
    // `ancientMemory` parameter of Interlocutor::sendRequest() with
    // Kind::CurationResult, which the interlocutors fold into the system block.
    static QString systemPrompt();

    // Builds the full curation user message from the three context sections.
    // All inputs are sanitized so that stray section markers in the
    // conversation cannot confuse the curation process.
    static QString buildUserMessage(QString recentContext, QString olderTranscript,
                                    QString existingMemory);

    // Formats a message list as a plain "user:/assistant:" transcript.
    static QString transcriptToText(const QList<ChatMessage> &messages);

    // Token weight of one message in the live context: API token counts when
    // available, otherwise a rough chars/4 estimate. Used symmetrically when
    // culling messages and when restoring them after a failed curation.
    static int estimateMessageTokens(const ChatMessage &msg);

    // Reads the ancient memory file; returns an empty string if absent.
    static QString loadMemory(const QString &memoryFilePath);

    // Backs up the existing memory file (timestamped .bak) then overwrites it.
    // Returns false (and writes nothing) if the backup could not be created.
    static bool saveMemoryWithBackup(const QString &memoryFilePath, const QString &content);
};

#endif // MEMORYCURATOR_H
// End Source File MemoryCurator.h
