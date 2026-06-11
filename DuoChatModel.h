// Begin Source File DuoChatModel.h
#ifndef DUOCHATMODEL_H
#define DUOCHATMODEL_H

#include <QAbstractListModel>
#include <QList>

#include "ChatMessage.h"
#include "Interlocutor.h"

// DuoChatModel drives a conversation between two AI interlocutors.
//
// It owns dedicated Interlocutor instances (separate from the one used by the
// human-facing ChatModel, so that signals and system prompts never interfere)
// and maintains a single shared transcript where each message is tagged with
// its speaker's name. For each API request the transcript is converted to the
// point of view of the side about to speak: its own messages become
// "assistant" turns and the partner's messages become "user" turns. This keeps
// the Interlocutor subclasses completely unchanged, including the strict
// user/assistant alternation required by the Anthropic API.
//
// The conversation opener is a synthetic kick-off prompt that is regenerated
// on the fly for the side that spoke first; it is never persisted, so the
// partner never sees it and reloaded conversations stay consistent.
class DuoChatModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum DuoMessageRoles {
        SpeakerRole = Qt::UserRole + 1,
        TextRole,
        TimestampRole,
        IsErrorRole,
        IsSideARole
    };
    Q_ENUM(DuoMessageRoles)

    explicit DuoChatModel(QObject *parent = nullptr);

    // Méthodes de QAbstractListModel
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Called by ChatManager: hands over two freshly created interlocutors
    // (ownership is transferred to this model) plus the file paths used for
    // per-side ancient memory (read-only here) and transcript persistence.
    void setParticipants(const QString &nameA, Interlocutor *interlocutorA,
                         const QString &memoryPathA, const QString &nameB,
                         Interlocutor *interlocutorB, const QString &memoryPathB,
                         const QString &transcriptFilePath);

    Q_INVOKABLE void start();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void clearConversation();

    Q_PROPERTY(QString participantA READ participantA NOTIFY participantsChanged)
    Q_PROPERTY(QString participantB READ participantB NOTIFY participantsChanged)
    Q_PROPERTY(bool sessionReady READ sessionReady NOTIFY participantsChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString pendingSpeaker READ pendingSpeaker NOTIFY busyChanged)
    Q_PROPERTY(int cumulativeTokenCost READ cumulativeTokenCost NOTIFY cumulativeTokenCostChanged)
    Q_PROPERTY(int maxTurns READ maxTurns WRITE setMaxTurns NOTIFY maxTurnsChanged)
    Q_PROPERTY(int turnsLeft READ turnsLeft NOTIFY turnsLeftChanged)

    QString participantA() const { return m_nameA; }
    QString participantB() const { return m_nameB; }
    bool sessionReady() const;
    bool running() const { return m_running; }
    bool busy() const { return m_busy; }
    QString pendingSpeaker() const { return m_pendingSpeaker; }
    int cumulativeTokenCost() const { return m_cumulativeTokenCost; }
    int maxTurns() const { return m_maxTurns; }
    void setMaxTurns(int maxTurns);
    int turnsLeft() const { return m_turnsLeft; }

signals:
    void participantsChanged();
    void runningChanged();
    void busyChanged();
    void cumulativeTokenCostChanged();
    void maxTurnsChanged();
    void turnsLeftChanged();

private:
    enum Side { SideA, SideB };

    Side nextSide() const;
    QList<ChatMessage> buildHistoryFor(Side side) const;
    void requestNextMessage();
    void onSideReply(Side side, const InterlocutorReply &reply);
    void onSideError(Side side, const QString &message);
    void appendMessage(const ChatMessage &message);
    void loadTranscript();

    QList<ChatMessage> m_messages;

    QString m_nameA;
    QString m_nameB;
    Interlocutor *m_interlocutorA = nullptr;
    Interlocutor *m_interlocutorB = nullptr;
    QString m_memoryPathA;
    QString m_memoryPathB;
    QString m_transcriptFilePath;

    bool m_running = false;
    bool m_busy = false;
    QString m_pendingSpeaker;
    int m_cumulativeTokenCost = 0;
    int m_maxTurns = 10;
    int m_turnsLeft = 0;
};

#endif // DUOCHATMODEL_H
// End Source File DuoChatModel.h
