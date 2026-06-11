// Begin Source File : ChatModel.cpp
#include "ChatModel.h"
#include "ChatManager.h"
#include "InterlocutorConfig.h"
#include "MemoryCurator.h"
#include "TetherLogger.h"
#include <QSettings>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

ChatModel::ChatModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_interlocutor(nullptr)
    , m_liveMemoryTokens(0)
    , m_cumulativeTokenCost(0)
    , m_curationTargetTokenCount(85001)
    , m_curationTriggerTokenCount(100001)
    , m_expectingContinuation(false)
    , m_extendedContextEnabled(false)
    , m_globalLogEnabled(false)
    , m_deepSeekNotesEnabled(true)
    , m_displayNotesEnabled(true)
{
    QSettings settings;
    m_extendedContextEnabled = settings.value("chat/extendedContextEnabled", false).toBool();
    m_globalLogEnabled = settings.value("chat/globalLogEnabled", false).toBool();
    m_deepSeekNotesEnabled = settings.value("chat/deepSeekNotesEnabled", true).toBool();
    m_displayNotesEnabled = settings.value("chat/displayNotesEnabled", true).toBool();
}

void ChatModel::setExtendedContextEnabled(bool enabled)
{
    if (m_extendedContextEnabled != enabled) {
        m_extendedContextEnabled = enabled;
        QSettings settings;
        settings.setValue("chat/extendedContextEnabled", enabled);
        emit extendedContextEnabledChanged();
        checkCurationThreshold();
    }
}

void ChatModel::setGlobalLogEnabled(bool enabled)
{
    if (m_globalLogEnabled != enabled) {
        m_globalLogEnabled = enabled;
        QSettings settings;
        settings.setValue("chat/globalLogEnabled", enabled);
        emit globalLogEnabledChanged();
    }
}

void ChatModel::setDeepSeekNotesEnabled(bool enabled)
{
    if (m_deepSeekNotesEnabled != enabled) {
        m_deepSeekNotesEnabled = enabled;
        QSettings settings;
        settings.setValue("chat/deepSeekNotesEnabled", enabled);
        emit deepSeekNotesEnabledChanged();
    }
}

void ChatModel::setDisplayNotesEnabled(bool enabled)
{
    if (m_displayNotesEnabled != enabled) {
        m_displayNotesEnabled = enabled;
        QSettings settings;
        settings.setValue("chat/displayNotesEnabled", enabled);
        emit displayNotesEnabledChanged();
    }
}

void ChatModel::setCurationThresholds(int triggerTokens, int targetTokens)
{
    if (triggerTokens <= targetTokens)
    {
        qWarning() << "Invalid curation thresholds: trigger (" << triggerTokens
                   << ") must be greater than target (" << targetTokens << "). Using defaults.";
        return;
    }
    m_curationTriggerTokenCount = triggerTokens;
    m_curationTargetTokenCount = targetTokens;
    qDebug() << "Curation thresholds updated: Trigger=" << m_curationTriggerTokenCount
             << ", Target=" << m_curationTargetTokenCount;

    // Check immediately if we need curation with new thresholds
    checkCurationThreshold();
}

ChatModel::~ChatModel()
{
    saveManagedFiles();
    saveChat(); // Sauvegarder la conversation à la fermeture si nécessaire
}

int ChatModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_messages.count();
}

QVariant ChatModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_messages.count())
        return QVariant();

    const ChatMessage &message = m_messages.at(index.row());
    switch (role)
    {
        case IsLocalMessageRole: return message.isLocalMessage();
        case TextRole: return message.text();
        case TimestampRole: return message.timestamp();
        case PromptTokensRole: return message.promptTokens();
        case CompletionTokensRole: return message.completionTokens();
        case RoleRole: return message.role();
        case IsTypingIndicatorRole: return message.isTypingIndicator;
        case IsErrorRole: return message.isError();
    }
    return QVariant();
}

QHash<int, QByteArray> ChatModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[IsLocalMessageRole] = "isLocalMessage";
    roles[TextRole] = "text";
    roles[TimestampRole] = "timestamp";
    roles[PromptTokensRole] = "promptTokens";
    roles[CompletionTokensRole] = "completionTokens";
    roles[RoleRole] = "role";
    roles[IsTypingIndicatorRole] = "isTypingIndicator";
    roles[IsErrorRole] = "isError";
    return roles;
}

void ChatModel::sendMessage(const QString &messageText)
{
    qDebug() << "sendMessage";
    if (m_isWaitingForReply)
    { // Empêcher d'envoyer un message pendant l'attente
        qDebug() << "aborted because we're waiting for reply";
        return;
    }

    if (!m_interlocutor)
    {
        m_expectingContinuation = false; // Reset state
        ChatMessage errorMessage(false, "Interlocutor is not set.", QDateTime::currentDateTime(), 0,
                                 0, "system", true);
        addMessage(errorMessage);
        return;
    }

    QStringList userAttachments;

    // 1. Ajouter le message de l'utilisateur au modèle
    // Note: les tokens de ce message seront déterminés par la réponse de l'API
    ChatMessage userMessage(true, messageText, QDateTime::currentDateTime(),
                            messageText.length() / 4, 0, "user");
    addMessage(userMessage);

    // 2. Ajouter les fichiers utilisateur qui sont prêts
    for (ManagedFile *file : m_managedFiles)
    {
        if (file->status() == ManagedFile::Ready && !file->fileId().isEmpty())
        {
            userAttachments.append(file->fileId());
        }
    }

    // 3. Charger la mémoire ancienne
    QString ancientMemory = loadOlderMemory();

    // 4. Envoyer la requête. Le ChatModel passe tout ce qu'il faut.
    m_interlocutor->sendRequest(m_messages, ancientMemory, InterlocutorReply::Kind::NormalMessage,
                                userAttachments);

    // 5. Afficher l'indicateur d'attente
    setWaitingForReply(true);
    m_expectingContinuation = false; // Reset state for new turn
    ChatMessage typingIndicator(false, "", QDateTime::currentDateTime(), 0, 0, "assistant");
    typingIndicator.isTypingIndicator = true;
    addMessage(typingIndicator);
}

void ChatModel::addMessage(const ChatMessage &message)
{
    beginInsertRows(QModelIndex(), m_messages.count(), m_messages.count());
    m_messages.append(message);
    endInsertRows();

    // Persister le message dans le fichier jsonl si ce n'est pas un typing
    // indicator ni une erreur
    if (!message.isTypingIndicator && !message.isError() && !m_currentChatFilePath.isEmpty())
    {
        QFile file(m_currentChatFilePath);
        if (file.open(QFile::Append | QFile::Text))
        {
            QTextStream stream(&file);
            stream << QJsonDocument(message.toJsonObject()).toJson(QJsonDocument::Compact) << "\n";
            file.close();
            TetherLogger::logMessage(m_interlocutor ? m_interlocutor->name() : "Unknown", message);
        }
        else
        {
            qWarning() << "Failed to open chat file for appending:" << m_currentChatFilePath;
        }
    }
    emit chatMessageAdded(message);
}

void ChatModel::setWaitingForReply(bool waiting)
{
    if (m_isWaitingForReply != waiting)
    {
        m_isWaitingForReply = waiting;
        emit isWaitingForReplyChanged();
    }
}

void ChatModel::loadChat(const QString &filePath)
{
    if (m_currentChatFilePath == filePath && !filePath.isEmpty())
    {
        qDebug() << "Chat file already loaded or path is the same:" << filePath;
        return;
    }

    // Sauvegarder le chat précédent si un fichier était ouvert
    saveChat();

    beginResetModel(); // Réinitialiser le modèle pour le chargement d'un nouveau
    // chat
    m_messages.clear();
    m_liveMemoryTokens = 0;    // Réinitialiser
    m_cumulativeTokenCost = 0; // Réinitialiser
    // Abandonner une éventuelle curation en vol : ses messages coupés
    // appartiennent à l'ancien chat (toujours intacts dans son fichier jsonl)
    // et sa réponse tardive sera ignorée grâce aux drapeaux remis à zéro.
    m_pendingCulledMessages.clear();
    m_isCurationInProgress = false;
    m_isWaitingForCurationResponse = false;
    // Vider les anciennes listes
    qDeleteAll(m_managedFiles);
    m_managedFiles.clear();
    m_messages.clear();

    if (filePath.isEmpty())
    {
        qDebug() << "No file path provided to load chat. Starting with an empty chat.";
        m_currentChatFilePath = ""; // S'assurer que le chemin est vide
        endResetModel();
        emit currentChatFilePathChanged();
        emit liveMemoryTokensChanged();
        return;
    }

    QFile file(filePath);
    if (!file.open(QFile::ReadOnly | QFile::Text))
    {
        qWarning() << "Failed to open chat file for reading:" << filePath;
        // Créer un nouveau fichier si inexistant
        if (!file.exists())
        {
            qDebug() << "Creating new chat file:" << filePath;
            file.open(QFile::WriteOnly | QFile::Text); // Crée le fichier vide
            file.close();
        }
        m_currentChatFilePath = filePath;
        endResetModel();
        emit currentChatFilePathChanged();
        emit liveMemoryTokensChanged();
        return;
    }

    QTextStream stream(&file);
    while (!stream.atEnd())
    {
        QByteArray line = stream.readLine().toUtf8();
        QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isNull() && doc.isObject())
        {
            ChatMessage msg = ChatMessage::fromJsonObject(doc.object());
            m_messages.append(msg);
            m_cumulativeTokenCost += msg.promptTokens() + msg.completionTokens();
        }
        else
        {
            qWarning() << "Skipping malformed JSON line in chat file:" << filePath;
        }
    }
    file.close();
    updateLiveMemoryEstimate();
    emit cumulativeTokenCostChanged();
    m_currentChatFilePath = filePath;
    loadManagedFiles(); // Charger la liste des fichiers associés

    endResetModel();

    emit currentChatFilePathChanged();
    emit liveMemoryTokensChanged();
    qDebug() << "Chat loaded from" << filePath << "with" << m_messages.count() << "messages and"
             << m_liveMemoryTokens << "tokens.";
    checkCurationThreshold();
}

void ChatModel::saveChat()
{
    // Le mécanisme de `addMessage` sauvegarde déjà chaque message
    // individuellement. Cette méthode `saveChat` serait utile si on voulait
    // réécrire tout le fichier après une modification ou une curation, ou nous
    // travaiilons avec un buffer en RAM. Pour l'instant, elle n'est pas
    // strictement nécessaire avec l'approche jsonl en append. On la laisse vide
    // pour l'instant ou on l'utilise pour une future réécriture complète.
    qDebug() << "ChatModel::saveChat() called. Doing nothing. Messages are saved "
                "incrementally.";
}

void ChatModel::clearChat()
{
    if (m_messages.isEmpty())
        return;

    beginRemoveRows(QModelIndex(), 0, m_messages.count() - 1);
    m_messages.clear();
    endRemoveRows();

    // L'utilisateur efface tout : la curation en vol (et ses messages coupés)
    // n'a plus d'objet, et sa réponse tardive sera ignorée.
    m_pendingCulledMessages.clear();
    m_isCurationInProgress = false;
    m_isWaitingForCurationResponse = false;

    // Effacer le fichier local associé
    if (!m_currentChatFilePath.isEmpty())
    {
        QFile file(m_currentChatFilePath);
        if (file.exists())
        {
            if (file.remove())
            {
                qDebug() << "Chat file removed:" << m_currentChatFilePath;
            }
            else
            {
                qWarning() << "Failed to remove chat file:" << m_currentChatFilePath;
            }
        }
    }
    m_liveMemoryTokens = 0;
    emit liveMemoryTokensChanged();
}

void ChatModel::reloadFromDisk()
{
    // Force un rechargement complet depuis le fichier jsonl, même si le chemin
    // n'a pas changé. Utilisé quand le journal a été modifié de l'extérieur
    // (par exemple par une conversation IA-IA impliquant l'interlocuteur actif).
    if (m_currentChatFilePath.isEmpty())
        return;
    const QString path = m_currentChatFilePath;
    m_currentChatFilePath.clear(); // Contourne le garde "déjà chargé" de loadChat
    loadChat(path);
}

void ChatModel::setCurrentChatFilePath(const QString &path)
{
    if (m_currentChatFilePath == path)
        return;
    // La logique de loadChat gère déjà la sauvegarde de l'ancien chat et le
    // changement de chemin
    loadChat(path); // Recharge le chat avec le nouveau chemin
}

void ChatModel::resetTokenCost()
{
    qDebug() << "Resetting cumulative token cost.";
    m_cumulativeTokenCost = 0;
    emit cumulativeTokenCostChanged();
}
void ChatModel::updateLiveMemoryEstimate()
{
    // Cette fonction est maintenant une estimation pour l'état initial.
    // La vraie valeur sera corrigée au premier appel API.
    int estimatedTokens = 15; // System prompt
    for (const auto &msg : m_messages)
    {
        estimatedTokens += msg.text().length() / 4;
    }

    if (m_liveMemoryTokens != estimatedTokens)
    {
        m_liveMemoryTokens = estimatedTokens;
        emit liveMemoryTokensChanged();
        qDebug() << "Estimated Live Memory on load:" << m_liveMemoryTokens << "tokens.";
    }
}

void ChatModel::checkCurationThreshold()
{
    int effectiveTrigger = m_curationTriggerTokenCount;

    if (m_extendedContextEnabled)
    {
        // TBD: Must be activated or deactivated depending on the model.
        // i.e. never activated on models without attached files!
        // Commented for now for use with DeepSeek chat/completion
        // effectiveTrigger += 25000; // Bonus context when no files attached
    }

    qDebug() << "There are currently" << m_liveMemoryTokens << "in the live memory. Trigger is"
             << effectiveTrigger << "(Extended:" << m_extendedContextEnabled << ")";
    if (m_liveMemoryTokens >= effectiveTrigger && !m_isCurationInProgress)
    {
        qDebug() << "Curation threshold reached! Live memory size:" << m_liveMemoryTokens;
        emit curationNeeded();
        triggerCuration();
    }
}
void ChatModel::onInterlocutorReply(const InterlocutorReply &reply)
{
    qDebug() << "ChatModel::onInterlocutorReply" << reply.text;
    if (reply.kind == InterlocutorReply::Kind::CurationResult)
    {
        if (!m_isWaitingForCurationResponse)
        {
            qWarning() << "Received CurationResult but was not waiting for one! Ignoring.";
            return;
        }
        m_isWaitingForCurationResponse = false;
        m_isCurationInProgress = false;
        handleCurationReply(reply);
        return;
    }

    handleNormalReply(reply);
}

void ChatModel::removeTypingIndicator(void)
{
    if (!m_messages.isEmpty() && m_messages.last().isTypingIndicator)
    {
        beginRemoveRows(QModelIndex(), m_messages.count() - 1, m_messages.count() - 1);
        m_messages.removeLast();
        endRemoveRows();
    }
    setWaitingForReply(false);
}

void ChatModel::onInterlocutorError(const QString &message)
{
    qWarning() << "Chat error:" << message;
    m_isWaitingForReply = false;
    if (m_isCurationInProgress)
    {
        // L'erreur peut venir de la requête de curation : on restaure les
        // messages coupés (le fichier jsonl n'a pas encore été réécrit) pour
        // qu'ils ne soient perdus ni du contexte ni du disque — notamment via
        // le rewriteChatFile() en fin de cette méthode.
        restoreCulledMessages();
        emit curationFinished(false);
    }
    m_isCurationInProgress = false;
    m_isWaitingForCurationResponse = false;
    removeTypingIndicator();

    if (!m_messages.isEmpty()) {
        ChatMessage &lastMsg = m_messages.last();
        if (lastMsg.isLocalMessage()) {
            lastMsg.setIsError(true);
            QModelIndex idx = index(m_messages.count() - 1);
            emit dataChanged(idx, idx, {IsErrorRole});
        }
    }

    // Ajouter le message d'erreur dans le chat
    ChatMessage errorMessage(false, message, QDateTime::currentDateTime(), 0, 0,
                             "system", // ou "assistant"
                             true);    // isError = true
    addMessage(errorMessage);

    rewriteChatFile();
}

void ChatModel::handleNormalReply(const InterlocutorReply &reply)
{
    // 1) Enlever le typing indicator
    removeTypingIndicator();

    // 2) Mettre à jour les compteurs de tokens avec les données propres de la
    // reply

    // 2a. Mettre à jour la taille de la Mémoire Vive pour la curation
    int newLiveMemorySize = reply.inputTokens + reply.outputTokens;
    if (m_liveMemoryTokens != newLiveMemorySize)
    {
        m_liveMemoryTokens = newLiveMemorySize;
        emit liveMemoryTokensChanged();
        qDebug() << "Live Memory size is now:" << m_liveMemoryTokens << "tokens.";
    }

    // 2b. Mettre à jour le coût cumulatif pour l'utilisateur
    m_cumulativeTokenCost += reply.totalTokens;
    emit cumulativeTokenCostChanged();
    qDebug() << "Cumulative token cost is now:" << m_cumulativeTokenCost;

    // 3) Créer un ChatMessage côté assistant avec reply.text
    // OU fusionner avec le précédent si on attend une suite

    if (m_expectingContinuation && !m_messages.isEmpty() &&
        m_messages.last().role() == "assistant" && !m_messages.last().isTypingIndicator)
    {
        // Fusionner
        qDebug() << "Merging continuation message...";
        ChatMessage &lastMsg = m_messages.last();
        lastMsg.setText(lastMsg.text() + reply.text);
        lastMsg.setCompletionTokens(lastMsg.completionTokens() + reply.outputTokens);
        // On pourrait aussi mettre à jour promptTokens si ça change, mais
        // généralement c'est le même contexte ou accumulé.

        // Notifier la vue que les données ont changé
        QModelIndex idx = index(m_messages.count() - 1);
        emit dataChanged(idx, idx, {TextRole, CompletionTokensRole});

        // Mettre à jour le fichier jsonl ?
        // C'est compliqué car on a déjà écrit la ligne.
        // Idéalement on devrait réécrire la dernière ligne ou tout le fichier.
        // Pour l'instant, on accepte que le fichier jsonl ait deux entrées séparées
        // (ce qui n'est pas grave, au rechargement ce sera deux messages) OU on
        // peut appeler rewriteChatFile() si on veut être propre. rewriteChatFile();
        // // Un peu lourd à chaque chunk.
    }
    else
    {
        // Nouveau message
        ChatMessage aiMessage(false, reply.text, QDateTime::currentDateTime(), reply.inputTokens,
                              reply.outputTokens, "assistant");

        // 4) L'ajouter à la liste + jsonl
        addMessage(aiMessage);
    }

    // Mise à jour de l'état d'attente
    if (reply.isIncomplete)
    {
        m_expectingContinuation = true;
        // On ne remet PAS waitingForReply à true car on a déjà reçu quelque chose,
        // mais on peut vouloir garder un indicateur visuel ?
        // Pour l'instant, l'utilisateur verra le texte s'afficher.
    }
    else
    {
        m_expectingContinuation = false;
    }

    // 5) Recalculer liveMemoryTokens + checkCurationThreshold()
    // L'estimation n'est plus nécessaire ici, la valeur exacte vient d'être mise
    // à jour. On lance juste la vérification.
    if (!reply.isIncomplete)
    {
        checkCurationThreshold();

        // Clear all attached files now that the response is complete
        while (!m_managedFiles.isEmpty())
        {
            deleteUserFile(0);
        }
    }
}

void ChatModel::handleCurationReply(const InterlocutorReply &reply)
{
    if (reply.isIncomplete)
    {
        qWarning() << "Curation failed: response was incomplete (reason:" << reply.text
                   << "). Restoring culled messages to prevent memory loss.";
        restoreCulledMessages();
        emit curationFinished(false);
        return;
    }

    const QString newSummary = reply.text.trimmed();
    if (newSummary.isEmpty())
    {
        qWarning() << "Curation failed: empty summary. Restoring culled messages.";
        restoreCulledMessages();
        emit curationFinished(false);
        return;
    }

    if (!saveOlderMemory(newSummary))
    {
        qWarning() << "Curation failed: could not save older memory. Restoring culled messages.";
        restoreCulledMessages();
        emit curationFinished(false);
        return;
    }

    if (m_interlocutor) {
        TetherLogger::logCuration(m_interlocutor->name(), newSummary);
    }

    // Le résumé est en sécurité : on peut maintenant retirer les messages
    // coupés du fichier jsonl.
    m_pendingCulledMessages.clear();
    rewriteChatFile();
    emit curationFinished(true);
}

void ChatModel::restoreCulledMessages()
{
    if (m_pendingCulledMessages.isEmpty())
        return;

    qDebug() << "Restoring" << m_pendingCulledMessages.count()
             << "culled messages into live memory.";
    beginInsertRows(QModelIndex(), 0, m_pendingCulledMessages.count() - 1);
    for (int i = m_pendingCulledMessages.size() - 1; i >= 0; --i)
    {
        const ChatMessage &msg = m_pendingCulledMessages.at(i);
        m_liveMemoryTokens += MemoryCurator::estimateMessageTokens(msg);
        m_messages.prepend(msg);
    }
    endInsertRows();
    m_pendingCulledMessages.clear();
    emit liveMemoryTokensChanged();
}

void ChatModel::setInterlocutor(Interlocutor *interlocutor)
{
    // Déconnecter l'ancien interlocuteur s'il existe
    if (m_interlocutor)
    {
        disconnect(m_interlocutor, &Interlocutor::replyReady, this,
                   &ChatModel::onInterlocutorReply);
        disconnect(m_interlocutor, &Interlocutor::errorOccurred, this,
                   &ChatModel::onInterlocutorError);
        disconnect(m_interlocutor, &Interlocutor::fileUploaded, this, &ChatModel::onFileUploaded);
        disconnect(m_interlocutor, &Interlocutor::fileUploadFailed, this,
                   &ChatModel::onFileUploadFailed);
        disconnect(m_interlocutor, &Interlocutor::fileDeleted, this, &ChatModel::onFileDeleted);
    }

    m_interlocutor = interlocutor;

    // Connecter le nouvel interlocuteur s'il n'est pas nul
    if (m_interlocutor)
    {
        connect(m_interlocutor, &Interlocutor::replyReady, this, &ChatModel::onInterlocutorReply);
        connect(m_interlocutor, &Interlocutor::errorOccurred, this,
                &ChatModel::onInterlocutorError);
        connect(m_interlocutor, &Interlocutor::fileUploaded, this, &ChatModel::onFileUploaded,
                Qt::UniqueConnection);
        connect(m_interlocutor, &Interlocutor::fileUploadFailed, this,
                &ChatModel::onFileUploadFailed);
        connect(m_interlocutor, &Interlocutor::fileDeleted, this, &ChatModel::onFileDeleted,
                Qt::UniqueConnection);
    }
}

void ChatModel::rewriteChatFile()
{
    if (m_currentChatFilePath.isEmpty())
    {
        return; // Pas de fichier à réécrire
    }

    QFile file(m_currentChatFilePath);
    // On ouvre en mode écriture seule, ce qui tronque (efface) le fichier
    // existant.
    if (!file.open(QFile::WriteOnly | QFile::Text))
    {
        qWarning() << "Failed to open chat file for rewriting:" << m_currentChatFilePath;
        return;
    }

    QTextStream stream(&file);
    for (const ChatMessage &message : m_messages)
    {
        if (!message.isError())
        {
            stream << QJsonDocument(message.toJsonObject()).toJson(QJsonDocument::Compact) << "\n";
        }
    }
    file.close();
    qDebug() << "Chat file rewritten successfully:" << m_currentChatFilePath;
}

void ChatModel::triggerCuration()
{
    if (m_isCurationInProgress)
        return;
    qDebug() << "Starting curation process... TargetTokens=" << m_curationTargetTokenCount;
    m_isCurationInProgress = true;

    // Cull en mémoire seulement : le fichier jsonl n'est réécrit qu'après un
    // résumé sauvegardé avec succès (handleCurationReply), pour ne jamais
    // perdre de contenu sans résumé. Même schéma que DuoChatModel.
    m_pendingCulledMessages.clear();
    while (m_liveMemoryTokens > m_curationTargetTokenCount && !m_messages.isEmpty())
    {
        ChatMessage msg = m_messages.first();
        m_pendingCulledMessages.append(msg);
        m_liveMemoryTokens -= MemoryCurator::estimateMessageTokens(msg);
        m_messages.removeFirst();
    }

    if (!m_pendingCulledMessages.isEmpty())
    {
        qDebug() << "Culling" << m_pendingCulledMessages.count() << "messages from live memory.";
        beginRemoveRows(QModelIndex(), 0, m_pendingCulledMessages.count() - 1);
        endRemoveRows();
        emit liveMemoryTokensChanged();
    }
    else
    {
        qWarning() << "Curation triggered, but no messages to cull. Aborting.";
        m_isCurationInProgress = false;
        return;
    }

    // --- Phase 2: Préparation de la requête de résumé (logique partagée avec
    // DuoChatModel via MemoryCurator)
    QString olderMemory = loadOlderMemory();
    QString conversationToSummarize = MemoryCurator::transcriptToText(m_pendingCulledMessages);
    // m_messages contient la Live Memory restante
    QString recentContext = MemoryCurator::transcriptToText(m_messages);

    QList<ChatMessage> curationHistory;
    QString curationUserMessage =
        MemoryCurator::buildUserMessage(recentContext, conversationToSummarize, olderMemory);

    qDebug() << "curationUserMessage=" << curationUserMessage;

    curationHistory.append(
        ChatMessage(true, curationUserMessage, QDateTime::currentDateTime(), 0, 0, "user"));

    // --- Phase 3: Appel à l'IA ---
    qDebug() << "Sending request for curation summary...";
    m_isWaitingForCurationResponse = true; // On lève le drapeau
    m_interlocutor->sendRequest(curationHistory, MemoryCurator::systemPrompt(),
                                InterlocutorReply::Kind::CurationResult, QStringList());
}

InterlocutorConfig *ChatModel::findCurrentConfig()
{
    QObject *parentObj = parent();
    if (parentObj)
    {
        ChatManager *chatManager = qobject_cast<ChatManager *>(parentObj);
        if (chatManager)
        {
            return chatManager->findCurrentConfig();
        }
    }
    return nullptr;
}

QString ChatModel::getOlderMemoryFilePath() const
{
    if (m_currentChatFilePath.isEmpty())
        return "";
    // On base le nom du fichier de mémoire sur celui du chat
    // ex: "default_chat.jsonl" -> "default_chat_memory.txt"
    QFileInfo fileInfo(m_currentChatFilePath);
    return fileInfo.path() + "/" + fileInfo.baseName() + "_memory.txt";
}

QString ChatModel::loadOlderMemory()
{
    return MemoryCurator::loadMemory(getOlderMemoryFilePath());
}

bool ChatModel::saveOlderMemory(const QString &content)
{
    return MemoryCurator::saveMemoryWithBackup(getOlderMemoryFilePath(), content);
}

QList<QObject *> ChatModel::managedFiles() const
{
    QList<QObject *> list;
    for (ManagedFile *file : m_managedFiles)
    {
        list.append(file);
    }
    return list;
}

void ChatModel::uploadUserFile(const QUrl &fileUrl)
{
    qDebug() << "uploadUserFile: fileUrl=" << fileUrl;
    if (!m_interlocutor)
        return;

    QFile file(fileUrl.toLocalFile());
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "Could not open file for upload:" << fileUrl.toLocalFile();
        return;
    }
    QByteArray content = file.readAll();

    // 1. Ajouter immédiatement à la liste avec le statut "Uploading"
    ManagedFile *newFile = new ManagedFile(fileUrl.fileName(), this);
    m_managedFiles.append(newFile);
    emit managedFilesChanged();

    // 2. Lancer l'upload
    // On passe un "purpose" spécial pour les fichiers utilisateur
    m_interlocutor->uploadFile(fileUrl.fileName(), content, "user_attachment");
}

void ChatModel::deleteUserFile(int index)
{
    if (index < 0 || index >= m_managedFiles.size())
        return;

    ManagedFile *fileToDelete = m_managedFiles.at(index);
    if (!fileToDelete->fileId().isEmpty())
    {
        m_interlocutor->deleteFile(fileToDelete->fileId());
    }

    m_managedFiles.removeAt(index);
    saveManagedFiles(); // SAUVEGARDER la liste après une suppression !

    emit managedFilesChanged();
    fileToDelete->deleteLater();
}

// Slot qui gère la fin de l'upload
void ChatModel::onFileUploaded(const QString &fileId, const QString &purpose)
{
    qDebug() << "onFileUploaded: fileId=" << fileId << "purpose=" << purpose;
    // --- Logique de tri ---
    if (purpose == "user_attachment") // <- User attachement is the only use of attached file
    // since the ancient memory and the curation no longer
    // rely on attached files
    {
        // C'est un fichier uploadé par l'utilisateur.
        qDebug() << "User file uploaded successfully. ID:" << fileId;
        for (int i = m_managedFiles.size() - 1; i >= 0; --i)
        {
            if (m_managedFiles[i]->status() == ManagedFile::Uploading)
            {
                m_managedFiles[i]->setFileId(fileId);
                m_managedFiles[i]->setStatus(ManagedFile::Ready);
                saveManagedFiles(); // SAUVEGARDER la liste après un upload réussi !
                return;             // On a trouvé et mis à jour le bon fichier
            }
        }
    }
    else
    {
        qWarning() << "WARNING: ChatModel::onFileUploaded received file_id" << fileId
                   << "with purpose" << purpose;
    }
}

void ChatModel::onFileDeleted(const QString &fileId, bool success)
{
    if (!success)
    {
        qWarning() << "WARNING: ChatModel::onFileDeleted FAILED to delete file_id" << fileId;
    }
}

void ChatModel::onFileUploadFailed(const QString &error)
{
    // The only kind of file we're uploading now is "user attachment", attached
    // files are no longer used internally for the live memory and the ancient
    // memory On doit savoir quel upload a échoué. On peut se baser sur les flags
    // d'état. C'était probablement un fichier utilisateur
    qWarning() << "User file upload failed:" << error;
    // On cherche le dernier fichier en "Uploading" pour le passer en "Error"
    for (int i = m_managedFiles.size() - 1; i >= 0; --i)
    {
        if (m_managedFiles[i]->status() == ManagedFile::Uploading)
        {
            m_managedFiles[i]->setStatus(ManagedFile::Error);
            break;
        }
    }
}

QString ChatModel::getManagedFilesPath() const
{
    if (m_currentChatFilePath.isEmpty())
        return "";
    QFileInfo fileInfo(m_currentChatFilePath);
    return fileInfo.path() + "/" + fileInfo.baseName() + "_files.json";
}

void ChatModel::loadManagedFiles()
{
    QString path = getManagedFilesPath();
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return; // Pas de fichier, pas de problème

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonArray fileArray = doc.array();

    for (const QJsonValue &value : fileArray)
    {
        ManagedFile *managedFile = ManagedFile::fromJsonObject(value.toObject(), this);
        m_managedFiles.append(managedFile);
    }

    emit managedFilesChanged();
    qDebug() << "Loaded" << m_managedFiles.count() << "managed files for this chat.";
}

void ChatModel::saveManagedFiles() const
{
    QString path = getManagedFilesPath();
    if (path.isEmpty())
        return;

    QJsonArray fileArray;
    for (const ManagedFile *file : m_managedFiles)
    {
        // On ne sauvegarde que les fichiers qui sont prêts et uploadés
        if (file->status() == ManagedFile::Ready)
        {
            fileArray.append(file->toJsonObject());
        }
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        qWarning() << "Could not open file list for writing:" << path;
        return;
    }
    file.write(QJsonDocument(fileArray).toJson(QJsonDocument::Indented));
}

// End Source File: ChatModel.cpp
