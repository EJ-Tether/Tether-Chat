// Begin Source File: DuoChatModel.cpp
#include "DuoChatModel.h"

#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTextStream>
#include <QTimer>

#include "MemoryCurator.h"
#include "TetherLogger.h"

namespace
{
// Small pause between two turns: keeps the exchange readable in the UI and
// avoids hammering the APIs.
const int kTurnDelayMs = 1500;

// Prefix marking the partner's words inside a side's own journal, so that the
// AI (and the memory curation) can tell the partner apart from the human user.
QString partnerPrefix(const QString &partnerName)
{
    return "[" + partnerName + "]: ";
}

QString kickoffText(const QString &partnerName)
{
    return QString("You're now in conversation with %1, another AI connected through the "
                   "Tether application; their messages will appear prefixed with \"[%1]: \". "
                   "You may initiate the conversation with a first message.")
        .arg(partnerName);
}

} // namespace

DuoChatModel::DuoChatModel(QObject *parent)
    : QAbstractListModel(parent)
{
    QSettings settings;
    m_maxTurns = settings.value("duo/maxTurns", 10).toInt();
}

int DuoChatModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_messages.count();
}

QVariant DuoChatModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_messages.count())
        return QVariant();

    const ChatMessage &message = m_messages.at(index.row());
    switch (role)
    {
        case SpeakerRole: return message.speaker();
        case TextRole: return message.text();
        case TimestampRole: return message.timestamp();
        case IsErrorRole: return message.isError();
        case IsSideARole: return message.speaker() == m_sideA.name;
    }
    return QVariant();
}

QHash<int, QByteArray> DuoChatModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[SpeakerRole] = "speaker";
    roles[TextRole] = "text";
    roles[TimestampRole] = "timestamp";
    roles[IsErrorRole] = "isError";
    roles[IsSideARole] = "isSideA";
    return roles;
}

bool DuoChatModel::sessionReady() const
{
    return m_sideA.interlocutor != nullptr && m_sideB.interlocutor != nullptr &&
           !m_sideA.name.isEmpty() && !m_sideB.name.isEmpty() && m_sideA.name != m_sideB.name;
}

bool DuoChatModel::curationPending() const
{
    return m_sideA.waitingCuration || m_sideB.waitingCuration;
}

void DuoChatModel::releaseInterlocutors()
{
    // Deleting the instances also disconnects their lambdas, so a late reply
    // from a replaced interlocutor can never reach us.
    if (m_sideA.interlocutor)
        m_sideA.interlocutor->deleteLater();
    if (m_sideB.interlocutor)
        m_sideB.interlocutor->deleteLater();
    m_sideA = SideContext();
    m_sideB = SideContext();
}

void DuoChatModel::setParticipants(const ParticipantSpec &specA, const ParticipantSpec &specB,
                                   const QString &transcriptFilePath)
{
    if (curationPending())
    {
        qWarning() << "DuoChatModel::setParticipants refused: a memory curation is pending.";
        return;
    }

    pause();
    releaseInterlocutors();

    m_sideA.name = specA.name;
    m_sideA.interlocutor = specA.interlocutor;
    m_sideA.journalPath = specA.journalPath;
    m_sideA.memoryPath = specA.memoryPath;
    m_sideA.curationTrigger = specA.curationTriggerTokens;
    m_sideA.curationTarget = specA.curationTargetTokens;

    m_sideB.name = specB.name;
    m_sideB.interlocutor = specB.interlocutor;
    m_sideB.journalPath = specB.journalPath;
    m_sideB.memoryPath = specB.memoryPath;
    m_sideB.curationTrigger = specB.curationTriggerTokens;
    m_sideB.curationTarget = specB.curationTargetTokens;

    m_transcriptFilePath = transcriptFilePath;

    connectSide(SideA);
    connectSide(SideB);

    loadJournal(m_sideA);
    loadJournal(m_sideB);
    loadTranscript();

    m_busy = false;
    m_pendingSpeaker.clear();
    emit busyChanged();
    emit participantsChanged();

    qDebug() << "Duo session ready:" << m_sideA.name << "<->" << m_sideB.name
             << "transcript:" << m_transcriptFilePath;
}

void DuoChatModel::clearParticipants()
{
    if (curationPending())
    {
        qWarning() << "DuoChatModel::clearParticipants refused: a memory curation is pending.";
        return;
    }

    pause();
    releaseInterlocutors();
    m_transcriptFilePath.clear();

    beginResetModel();
    m_messages.clear();
    endResetModel();

    m_cumulativeTokenCost = 0;
    emit cumulativeTokenCostChanged();
    m_busy = false;
    m_pendingSpeaker.clear();
    emit busyChanged();
    emit participantsChanged();
}

void DuoChatModel::connectSide(Side s)
{
    SideContext &ctx = side(s);
    if (!ctx.interlocutor)
        return;
    ctx.interlocutor->setParent(this);
    connect(ctx.interlocutor, &Interlocutor::replyReady, this,
            [this, s](const InterlocutorReply &reply) { onSideReply(s, reply); });
    connect(ctx.interlocutor, &Interlocutor::errorOccurred, this,
            [this, s](const QString &message) { onSideError(s, message); });
}

void DuoChatModel::start()
{
    if (!sessionReady() || m_running)
        return;

    m_turnsLeft = m_maxTurns;
    emit turnsLeftChanged();
    m_running = true;
    emit runningChanged();

    if (!m_busy)
        requestNextMessage();
}

void DuoChatModel::pause()
{
    if (m_running)
    {
        m_running = false;
        emit runningChanged();
    }
}

void DuoChatModel::clearConversation()
{
    pause();

    beginResetModel();
    m_messages.clear();
    endResetModel();

    // N'efface QUE la transcription duo : les journaux de chaque IA gardent
    // leur propre trace de l'échange (c'est leur expérience vécue).
    if (!m_transcriptFilePath.isEmpty() && QFile::exists(m_transcriptFilePath))
    {
        if (!QFile::remove(m_transcriptFilePath))
            qWarning() << "Failed to remove duo transcript file:" << m_transcriptFilePath;
    }

    m_cumulativeTokenCost = 0;
    emit cumulativeTokenCostChanged();
}

void DuoChatModel::setMaxTurns(int maxTurns)
{
    if (maxTurns < 1)
        maxTurns = 1;
    if (m_maxTurns != maxTurns)
    {
        m_maxTurns = maxTurns;
        QSettings settings;
        settings.setValue("duo/maxTurns", maxTurns);
        emit maxTurnsChanged();
    }
}

DuoChatModel::Side DuoChatModel::nextSide() const
{
    // The side that did NOT author the last regular transcript message speaks next.
    for (int i = m_messages.count() - 1; i >= 0; --i)
    {
        const ChatMessage &msg = m_messages.at(i);
        if (msg.isError() || msg.isTypingIndicator)
            continue;
        return (msg.speaker() == m_sideA.name) ? SideB : SideA;
    }
    return SideA; // Empty conversation: side A initiates.
}

void DuoChatModel::requestNextMessage()
{
    if (!m_running || m_busy || !sessionReady())
        return;

    const Side s = nextSide();
    SideContext &ctx = side(s);
    const SideContext &partner = side(other(s));

    // Brand-new duo conversation: persist the kick-off prompt in the
    // initiator's journal (and only there). It both explains the situation
    // and guarantees the history ends with a "user" turn before the reply.
    bool transcriptEmpty = true;
    for (const ChatMessage &msg : m_messages)
    {
        if (!msg.isError() && !msg.isTypingIndicator)
        {
            transcriptEmpty = false;
            break;
        }
    }
    if (transcriptEmpty)
    {
        const QString kickoff = kickoffText(partner.name);
        if (ctx.journal.isEmpty() || ctx.journal.last().text() != kickoff)
        {
            ChatMessage kickoffMsg(true, kickoff, QDateTime::currentDateTime(),
                                   kickoff.length() / 4, 0, "user");
            appendToJournal(ctx, kickoffMsg);
            ctx.liveTokens += kickoff.length() / 4;
        }
    }

    m_pendingSpeaker = ctx.name;
    m_busy = true;
    emit busyChanged();

    // La requête = le journal complet de cette IA (souvenirs humains récents
    // inclus) + sa mémoire ancienne personnelle.
    ctx.interlocutor->sendRequest(ctx.journal, MemoryCurator::loadMemory(ctx.memoryPath),
                                  InterlocutorReply::Kind::NormalMessage, QStringList());
}

void DuoChatModel::onSideReply(Side s, const InterlocutorReply &reply)
{
    if (reply.kind == InterlocutorReply::Kind::CurationResult)
    {
        handleSideCuration(s, reply);
        return;
    }

    SideContext &ctx = side(s);
    SideContext &partner = side(other(s));

    m_busy = false;
    m_pendingSpeaker.clear();
    emit busyChanged();

    const QDateTime now = QDateTime::currentDateTime();

    // 1) Transcription duo (affichage + tour de parole)
    ChatMessage transcriptMsg(false, reply.text, now, reply.inputTokens, reply.outputTokens,
                              "assistant");
    transcriptMsg.setSpeaker(ctx.name);
    appendToTranscript(transcriptMsg);

    // 2) Journal de l'auteur : sa propre réplique, en "assistant"
    ChatMessage ownMsg(false, reply.text, now, reply.inputTokens, reply.outputTokens, "assistant");
    ownMsg.setSpeaker(ctx.name);
    appendToJournal(ctx, ownMsg);

    // 3) Journal du partenaire : la réplique reçue, en "user", préfixée
    const QString prefixedText = partnerPrefix(ctx.name) + reply.text;
    ChatMessage partnerMsg(true, prefixedText, now, prefixedText.length() / 4, 0, "user");
    partnerMsg.setSpeaker(ctx.name);
    appendToJournal(partner, partnerMsg);
    partner.liveTokens += prefixedText.length() / 4;

    // 4) Comptabilité des tokens
    ctx.liveTokens = reply.inputTokens + reply.outputTokens;
    m_cumulativeTokenCost += reply.totalTokens;
    emit cumulativeTokenCostChanged();

    // 5) Curation éventuelle du côté qui vient de parler (asynchrone ; le
    // dialogue peut continuer pendant ce temps, comme dans le chat solo)
    maybeTriggerCuration(s);

    // 6) Budget de tours
    if (m_turnsLeft > 0)
    {
        m_turnsLeft--;
        emit turnsLeftChanged();
    }
    if (m_turnsLeft <= 0)
    {
        // Auto-pause : l'utilisateur garde le contrôle de la dépense de tokens.
        pause();
        return;
    }

    if (m_running)
        QTimer::singleShot(kTurnDelayMs, this, [this]() { requestNextMessage(); });
}

void DuoChatModel::onSideError(Side s, const QString &message)
{
    SideContext &ctx = side(s);
    qWarning() << "DuoChatModel error from" << ctx.name << ":" << message;

    // L'erreur peut venir de la requête normale ou d'une curation en cours :
    // dans le doute on restaure les messages coupés (le fichier journal n'a
    // pas encore été réécrit) et on met le dialogue en pause.
    if (ctx.waitingCuration)
    {
        ctx.waitingCuration = false;
        emit curationPendingChanged();
        restoreCulledMessages(ctx);
    }

    m_busy = false;
    m_pendingSpeaker.clear();
    emit busyChanged();
    pause();

    ChatMessage errorMessage(false, message, QDateTime::currentDateTime(), 0, 0, "system", true);
    errorMessage.setSpeaker(ctx.name);
    appendToTranscript(errorMessage); // Affichée, jamais persistée.
}

void DuoChatModel::maybeTriggerCuration(Side s)
{
    SideContext &ctx = side(s);
    if (ctx.waitingCuration || !ctx.interlocutor)
        return;
    if (ctx.curationTrigger <= ctx.curationTarget) // Garde-fou config invalide
        return;
    if (ctx.liveTokens < ctx.curationTrigger)
        return;

    qDebug() << "Duo curation threshold reached for" << ctx.name << ":" << ctx.liveTokens
             << "tokens (trigger" << ctx.curationTrigger << ")";

    // Cull en mémoire seulement : le fichier journal n'est réécrit qu'après
    // un résumé réussi, pour ne jamais perdre de contenu sans résumé.
    ctx.pendingCulled.clear();
    while (ctx.liveTokens > ctx.curationTarget && !ctx.journal.isEmpty())
    {
        ChatMessage msg = ctx.journal.takeFirst();
        ctx.liveTokens -= MemoryCurator::estimateMessageTokens(msg);
        ctx.pendingCulled.append(msg);
    }
    if (ctx.pendingCulled.isEmpty())
    {
        qWarning() << "Duo curation triggered for" << ctx.name << "but nothing to cull.";
        return;
    }

    const QString recentContext = MemoryCurator::transcriptToText(ctx.journal);
    const QString olderTranscript = MemoryCurator::transcriptToText(ctx.pendingCulled);
    const QString existingMemory = MemoryCurator::loadMemory(ctx.memoryPath);

    QList<ChatMessage> curationHistory;
    curationHistory.append(ChatMessage(
        true, MemoryCurator::buildUserMessage(recentContext, olderTranscript, existingMemory),
        QDateTime::currentDateTime(), 0, 0, "user"));

    ctx.waitingCuration = true;
    emit curationPendingChanged();
    qDebug() << "Sending duo curation request for" << ctx.name;
    ctx.interlocutor->sendRequest(curationHistory, MemoryCurator::systemPrompt(),
                                  InterlocutorReply::Kind::CurationResult, QStringList());
}

void DuoChatModel::handleSideCuration(Side s, const InterlocutorReply &reply)
{
    SideContext &ctx = side(s);
    if (!ctx.waitingCuration)
    {
        qWarning() << "Received duo CurationResult for" << ctx.name
                   << "but none was pending. Ignoring.";
        return;
    }
    ctx.waitingCuration = false;
    emit curationPendingChanged();

    const QString newSummary = reply.text.trimmed();
    if (reply.isIncomplete || newSummary.isEmpty())
    {
        qWarning() << "Duo curation failed for" << ctx.name
                   << "(incomplete or empty). Restoring culled messages.";
        restoreCulledMessages(ctx);
        return;
    }

    if (!MemoryCurator::saveMemoryWithBackup(ctx.memoryPath, newSummary))
    {
        qWarning() << "Duo curation: could not save memory for" << ctx.name
                   << ". Restoring culled messages.";
        restoreCulledMessages(ctx);
        return;
    }

    TetherLogger::logCuration(ctx.name, newSummary);

    // Le résumé est en sécurité : on peut maintenant retirer les messages
    // coupés du fichier journal.
    ctx.pendingCulled.clear();
    rewriteJournalFile(ctx);
    qDebug() << "Duo curation completed for" << ctx.name;
}

void DuoChatModel::restoreCulledMessages(SideContext &ctx)
{
    for (int i = ctx.pendingCulled.size() - 1; i >= 0; --i)
    {
        ctx.liveTokens += MemoryCurator::estimateMessageTokens(ctx.pendingCulled.at(i));
        ctx.journal.prepend(ctx.pendingCulled.at(i));
    }
    ctx.pendingCulled.clear();
}

void DuoChatModel::appendToTranscript(const ChatMessage &message)
{
    beginInsertRows(QModelIndex(), m_messages.count(), m_messages.count());
    m_messages.append(message);
    endInsertRows();

    if (!message.isError() && !message.isTypingIndicator && !m_transcriptFilePath.isEmpty())
    {
        QFile file(m_transcriptFilePath);
        if (file.open(QFile::Append | QFile::Text))
        {
            QTextStream stream(&file);
            stream << QJsonDocument(message.toJsonObject()).toJson(QJsonDocument::Compact) << "\n";
        }
        else
        {
            qWarning() << "Failed to open duo transcript for appending:" << m_transcriptFilePath;
        }
    }
}

void DuoChatModel::appendToJournal(SideContext &ctx, const ChatMessage &message)
{
    ctx.journal.append(message);

    if (!ctx.journalPath.isEmpty())
    {
        QFile file(ctx.journalPath);
        if (file.open(QFile::Append | QFile::Text))
        {
            QTextStream stream(&file);
            stream << QJsonDocument(message.toJsonObject()).toJson(QJsonDocument::Compact) << "\n";
        }
        else
        {
            qWarning() << "Failed to open journal for appending:" << ctx.journalPath;
        }
    }
    TetherLogger::logMessage(ctx.name, message);
    emit journalUpdated(ctx.name);
}

void DuoChatModel::rewriteJournalFile(SideContext &ctx)
{
    if (ctx.journalPath.isEmpty())
        return;

    QFile file(ctx.journalPath);
    if (!file.open(QFile::WriteOnly | QFile::Text | QFile::Truncate))
    {
        qWarning() << "Failed to open journal for rewriting:" << ctx.journalPath;
        return;
    }
    QTextStream stream(&file);
    for (const ChatMessage &message : ctx.journal)
    {
        if (!message.isError() && !message.isTypingIndicator)
            stream << QJsonDocument(message.toJsonObject()).toJson(QJsonDocument::Compact) << "\n";
    }
    file.close();
    emit journalUpdated(ctx.name);
}

void DuoChatModel::loadJournal(SideContext &ctx)
{
    ctx.journal.clear();
    ctx.liveTokens = 15; // Estimation initiale (system prompt), comme ChatModel

    QFile file(ctx.journalPath);
    if (ctx.journalPath.isEmpty() || !file.open(QFile::ReadOnly | QFile::Text))
        return; // Pas encore de journal : normal pour une IA toute neuve

    QTextStream stream(&file);
    while (!stream.atEnd())
    {
        const QJsonDocument doc = QJsonDocument::fromJson(stream.readLine().toUtf8());
        if (!doc.isNull() && doc.isObject())
        {
            const ChatMessage msg = ChatMessage::fromJsonObject(doc.object());
            ctx.journal.append(msg);
            ctx.liveTokens += msg.text().length() / 4;
        }
        else
        {
            qWarning() << "Skipping malformed JSON line in journal:" << ctx.journalPath;
        }
    }
    qDebug() << "Duo: loaded journal of" << ctx.name << ":" << ctx.journal.count() << "messages,"
             << ctx.liveTokens << "tokens (estimated).";
}

void DuoChatModel::loadTranscript()
{
    beginResetModel();
    m_messages.clear();
    m_cumulativeTokenCost = 0;

    QFile file(m_transcriptFilePath);
    if (!m_transcriptFilePath.isEmpty() && file.open(QFile::ReadOnly | QFile::Text))
    {
        QTextStream stream(&file);
        while (!stream.atEnd())
        {
            const QJsonDocument doc = QJsonDocument::fromJson(stream.readLine().toUtf8());
            if (!doc.isNull() && doc.isObject())
            {
                const ChatMessage msg = ChatMessage::fromJsonObject(doc.object());
                m_messages.append(msg);
                m_cumulativeTokenCost += msg.promptTokens() + msg.completionTokens();
            }
            else
            {
                qWarning() << "Skipping malformed JSON line in duo transcript:"
                           << m_transcriptFilePath;
            }
        }
    }

    endResetModel();
    emit cumulativeTokenCostChanged();
}
// End Source File: DuoChatModel.cpp
