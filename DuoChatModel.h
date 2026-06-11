// Begin Source File DuoChatModel.h
#ifndef DUOCHATMODEL_H
#define DUOCHATMODEL_H

#include <QAbstractListModel>
#include <QList>

#include "ChatMessage.h"
#include "Interlocutor.h"

// DuoChatModel drives a conversation between two AI interlocutors.
//
// Identity continuity design: each side keeps its OWN rolling context — the
// very same journal file (<name>.jsonl) and long-term memory file
// (<name>_memory.txt) used by its human-facing chat. Every duo message is
// appended to both sides' journals from each side's perspective:
//   - its own words are stored as "assistant" turns;
//   - the partner's words are stored as "user" turns, prefixed with
//     "[PartnerName]: " so that the AI (and the later memory curation) can
//     always tell the partner apart from the human user.
// Each side's API request is simply its full journal, so the AI also
// remembers its recent exchanges with the human verbatim, and once back in
// the human-facing chat it remembers the duo conversation — verbatim while
// recent, curated into long-term memory when old.
//
// Each side runs the standard memory curation cycle (shared with ChatModel
// through MemoryCurator) when its own context threshold is exceeded. As in
// ChatModel, the journal file is only rewritten on disk AFTER a successful
// summary; on failure the culled messages are restored in memory.
//
// The model also maintains the duo transcript (duo_<A>__<B>.jsonl) as the
// display/persistence backbone of the "AI ↔ AI" tab: it determines whose turn
// it is and survives application restarts.
//
// DuoChatModel owns dedicated Interlocutor instances (separate from the one
// used by ChatModel, so that signals and system prompts never interfere).
// The conversation opener is a kick-off prompt persisted in the initiator's
// journal only; the partner never sees it.
class DuoChatModel : public QAbstractListModel
{
    Q_OBJECT

public:
    // Everything ChatManager needs to hand over for one side of the dialogue.
    struct ParticipantSpec
    {
        QString name;
        Interlocutor *interlocutor = nullptr;
        QString journalPath; // <name>.jsonl — same file as the human-facing chat
        QString memoryPath;  // <name>_memory.txt
        int curationTriggerTokens = 100000;
        int curationTargetTokens = 85000;
    };

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

    // Called by ChatManager. Ownership of the interlocutors is transferred to
    // this model. Refused while a memory curation is still pending.
    void setParticipants(const ParticipantSpec &specA, const ParticipantSpec &specB,
                         const QString &transcriptFilePath);
    // Invalidates the current session (Start becomes unavailable in the UI).
    void clearParticipants();

    Q_INVOKABLE void start();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void clearConversation();

    Q_PROPERTY(QString participantA READ participantA NOTIFY participantsChanged)
    Q_PROPERTY(QString participantB READ participantB NOTIFY participantsChanged)
    Q_PROPERTY(bool sessionReady READ sessionReady NOTIFY participantsChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString pendingSpeaker READ pendingSpeaker NOTIFY busyChanged)
    Q_PROPERTY(bool curationPending READ curationPending NOTIFY curationPendingChanged)
    Q_PROPERTY(int cumulativeTokenCost READ cumulativeTokenCost NOTIFY cumulativeTokenCostChanged)
    Q_PROPERTY(int maxTurns READ maxTurns WRITE setMaxTurns NOTIFY maxTurnsChanged)
    Q_PROPERTY(int turnsLeft READ turnsLeft NOTIFY turnsLeftChanged)

    QString participantA() const { return m_sideA.name; }
    QString participantB() const { return m_sideB.name; }
    bool sessionReady() const;
    bool running() const { return m_running; }
    bool busy() const { return m_busy; }
    QString pendingSpeaker() const { return m_pendingSpeaker; }
    bool curationPending() const;
    int cumulativeTokenCost() const { return m_cumulativeTokenCost; }
    int maxTurns() const { return m_maxTurns; }
    void setMaxTurns(int maxTurns);
    int turnsLeft() const { return m_turnsLeft; }

signals:
    void participantsChanged();
    void runningChanged();
    void busyChanged();
    void curationPendingChanged();
    void cumulativeTokenCostChanged();
    void maxTurnsChanged();
    void turnsLeftChanged();
    // Émis quand le journal solo d'un interlocuteur a été modifié par le duo
    // (ChatManager s'en sert pour recharger le chat principal si besoin).
    void journalUpdated(const QString &interlocutorName);

private:
    enum Side { SideA, SideB };

    struct SideContext
    {
        QString name;
        Interlocutor *interlocutor = nullptr;
        QString journalPath;
        QString memoryPath;
        int curationTrigger = 100000;
        int curationTarget = 85000;

        QList<ChatMessage> journal; // Rolling context de cette IA (en mémoire)
        int liveTokens = 0;         // Taille de contexte, corrigée à chaque réponse API
        bool waitingCuration = false;
        QList<ChatMessage> pendingCulled; // Coupés du contexte, pas encore validés sur disque
    };

    SideContext &side(Side s) { return (s == SideA) ? m_sideA : m_sideB; }
    const SideContext &side(Side s) const { return (s == SideA) ? m_sideA : m_sideB; }
    static Side other(Side s) { return (s == SideA) ? SideB : SideA; }

    Side nextSide() const;
    void connectSide(Side s);
    void requestNextMessage();
    void onSideReply(Side s, const InterlocutorReply &reply);
    void onSideError(Side s, const QString &message);
    void maybeTriggerCuration(Side s);
    void handleSideCuration(Side s, const InterlocutorReply &reply);
    void restoreCulledMessages(SideContext &ctx);

    void appendToTranscript(const ChatMessage &message);
    void appendToJournal(SideContext &ctx, const ChatMessage &message);
    void rewriteJournalFile(SideContext &ctx);
    void loadJournal(SideContext &ctx);
    void loadTranscript();
    void releaseInterlocutors();

    QList<ChatMessage> m_messages; // Transcription duo (affichage + tour de parole)

    SideContext m_sideA;
    SideContext m_sideB;
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
