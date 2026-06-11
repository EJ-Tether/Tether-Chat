// Begin Source File: DuoChatModel.cpp
#include "DuoChatModel.h"

#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTextStream>
#include <QTimer>

namespace
{
// Small pause between two turns: keeps the exchange readable in the UI and
// avoids hammering the APIs.
const int kTurnDelayMs = 1500;

QString readFileText(const QString &path)
{
    if (path.isEmpty())
        return {};
    QFile file(path);
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return {};
    QTextStream in(&file);
    return in.readAll();
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
        case IsSideARole: return message.speaker() == m_nameA;
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
    return m_interlocutorA != nullptr && m_interlocutorB != nullptr && !m_nameA.isEmpty() &&
           !m_nameB.isEmpty() && m_nameA != m_nameB;
}

void DuoChatModel::setParticipants(const QString &nameA, Interlocutor *interlocutorA,
                                   const QString &memoryPathA, const QString &nameB,
                                   Interlocutor *interlocutorB, const QString &memoryPathB,
                                   const QString &transcriptFilePath)
{
    pause();

    // Deleting the previous instances also disconnects their lambdas, so a
    // late reply from a replaced interlocutor can never reach us.
    if (m_interlocutorA)
        m_interlocutorA->deleteLater();
    if (m_interlocutorB)
        m_interlocutorB->deleteLater();

    m_nameA = nameA;
    m_interlocutorA = interlocutorA;
    m_memoryPathA = memoryPathA;
    m_nameB = nameB;
    m_interlocutorB = interlocutorB;
    m_memoryPathB = memoryPathB;
    m_transcriptFilePath = transcriptFilePath;

    if (m_interlocutorA)
    {
        m_interlocutorA->setParent(this);
        connect(m_interlocutorA, &Interlocutor::replyReady, this,
                [this](const InterlocutorReply &reply) { onSideReply(SideA, reply); });
        connect(m_interlocutorA, &Interlocutor::errorOccurred, this,
                [this](const QString &message) { onSideError(SideA, message); });
    }
    if (m_interlocutorB)
    {
        m_interlocutorB->setParent(this);
        connect(m_interlocutorB, &Interlocutor::replyReady, this,
                [this](const InterlocutorReply &reply) { onSideReply(SideB, reply); });
        connect(m_interlocutorB, &Interlocutor::errorOccurred, this,
                [this](const QString &message) { onSideError(SideB, message); });
    }

    loadTranscript();

    m_busy = false;
    m_pendingSpeaker.clear();
    emit busyChanged();
    emit participantsChanged();

    qDebug() << "Duo session ready:" << m_nameA << "<->" << m_nameB
             << "transcript:" << m_transcriptFilePath;
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
    // The side that did NOT author the last regular message speaks next.
    for (int i = m_messages.count() - 1; i >= 0; --i)
    {
        const ChatMessage &msg = m_messages.at(i);
        if (msg.isError() || msg.isTypingIndicator)
            continue;
        return (msg.speaker() == m_nameA) ? SideB : SideA;
    }
    return SideA; // Empty conversation: side A initiates.
}

QList<ChatMessage> DuoChatModel::buildHistoryFor(Side side) const
{
    const QString self = (side == SideA) ? m_nameA : m_nameB;
    const QString partner = (side == SideA) ? m_nameB : m_nameA;

    QList<ChatMessage> history;

    // The initiating side has no incoming message to react to, so it receives
    // a synthetic kick-off prompt. It is regenerated here instead of being
    // persisted: the partner never sees it, and the perspective stays valid
    // when a saved conversation is reloaded. It also guarantees that the
    // history starts with a "user" turn, as required by some APIs.
    bool selfSpeaksFirst = true;
    for (const ChatMessage &msg : m_messages)
    {
        if (msg.isError() || msg.isTypingIndicator)
            continue;
        selfSpeaksFirst = (msg.speaker() == self);
        break;
    }
    if (selfSpeaksFirst)
    {
        const QString kickoff = QString("You're now in conversation with %1, you may initiate "
                                        "the conversation with a first message.")
                                    .arg(partner);
        history.append(ChatMessage(true, kickoff, QDateTime::currentDateTime(),
                                   kickoff.length() / 4, 0, "user"));
    }

    for (const ChatMessage &msg : m_messages)
    {
        if (msg.isError() || msg.isTypingIndicator)
            continue;
        const bool own = (msg.speaker() == self);
        history.append(ChatMessage(!own, msg.text(), msg.timestamp(), msg.promptTokens(),
                                   msg.completionTokens(), own ? "assistant" : "user"));
    }
    return history;
}

void DuoChatModel::requestNextMessage()
{
    if (!m_running || m_busy || !sessionReady())
        return;

    const Side side = nextSide();
    Interlocutor *target = (side == SideA) ? m_interlocutorA : m_interlocutorB;
    const QString memoryPath = (side == SideA) ? m_memoryPathA : m_memoryPathB;

    m_pendingSpeaker = (side == SideA) ? m_nameA : m_nameB;
    m_busy = true;
    emit busyChanged();

    // Each side receives its own personal ancient memory (the one curated in
    // its human-facing chat), read-only: the duo conversation never rewrites it.
    target->sendRequest(buildHistoryFor(side), readFileText(memoryPath),
                        InterlocutorReply::Kind::NormalMessage, QStringList());
}

void DuoChatModel::onSideReply(Side side, const InterlocutorReply &reply)
{
    if (reply.kind != InterlocutorReply::Kind::NormalMessage)
    {
        qWarning() << "DuoChatModel: unexpected reply kind, ignoring.";
        return;
    }

    m_busy = false;
    m_pendingSpeaker.clear();
    emit busyChanged();

    ChatMessage message(false, reply.text, QDateTime::currentDateTime(), reply.inputTokens,
                        reply.outputTokens, "assistant");
    message.setSpeaker(side == SideA ? m_nameA : m_nameB);
    appendMessage(message);

    m_cumulativeTokenCost += reply.totalTokens;
    emit cumulativeTokenCostChanged();

    if (m_turnsLeft > 0)
    {
        m_turnsLeft--;
        emit turnsLeftChanged();
    }

    if (m_turnsLeft <= 0)
    {
        // Turn budget exhausted: auto-pause so the user keeps control of the
        // token spending. Pressing Start resumes for another run.
        pause();
        return;
    }

    if (m_running)
        QTimer::singleShot(kTurnDelayMs, this, [this]() { requestNextMessage(); });
}

void DuoChatModel::onSideError(Side side, const QString &message)
{
    qWarning() << "DuoChatModel error from" << (side == SideA ? m_nameA : m_nameB) << ":"
               << message;

    m_busy = false;
    m_pendingSpeaker.clear();
    emit busyChanged();
    pause();

    ChatMessage errorMessage(false, message, QDateTime::currentDateTime(), 0, 0, "system", true);
    errorMessage.setSpeaker(side == SideA ? m_nameA : m_nameB);
    appendMessage(errorMessage); // Errors are displayed but never persisted.
}

void DuoChatModel::appendMessage(const ChatMessage &message)
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
