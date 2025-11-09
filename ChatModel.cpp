// Begin Source File : ChatModel.cpp
#include "ChatModel.h"
#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include "InterlocutorConfig.h"
#include "ChatManager.h"

ChatModel::ChatModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_interlocutor(nullptr)
    , m_liveMemoryTokens(0)
    , m_cumulativeTokenCost(0)
{
    if (m_interlocutor) {
        connect(m_interlocutor,
                &Interlocutor::responseReceived,
                this,
                &ChatModel::handleInterlocutorResponse);
        connect(m_interlocutor,
                &Interlocutor::errorOccurred,
                this,
                &ChatModel::handleInterlocutorError);
        connect(m_interlocutor, &Interlocutor::fileUploaded, this, &ChatModel::onFileUploaded);
        connect(m_interlocutor,
                &Interlocutor::fileUploadFailed,
                this,
                &ChatModel::onFileUploadFailed);
        connect(m_interlocutor, &Interlocutor::fileDeleted, this, &ChatModel::onFileDeleted);
    }
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
    switch (role) {
    case IsLocalMessageRole:
        return message.isLocalMessage();
    case TextRole:
        return message.text();
    case TimestampRole:
        return message.timestamp();
    case PromptTokensRole:
        return message.promptTokens();
    case CompletionTokensRole:
        return message.completionTokens();
    case RoleRole:
        return message.role();
    case IsTypingIndicatorRole:
        return message.isTypingIndicator;
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
    return roles;
}

void ChatModel::sendMessage(const QString &messageText)
{
    if (m_isWaitingForReply) // Empêcher d'envoyer un message pendant l'attente
        return;

    if (!m_interlocutor) {
        emit chatError("Interlocutor is not set.");
        return;
    }

    QStringList userAttachments;

    // 1. Ajouter le message de l'utilisateur au modèle
    // Note: les tokens de ce message seront déterminés par la réponse de l'API
    ChatMessage userMessage(true,
                            messageText,
                            QDateTime::currentDateTime(),
                            messageText.length() / 4,
                            0,
                            "user");
    addMessage(userMessage);

    // 2. Ajouter les fichiers utilisateur qui sont prêts
    for (ManagedFile *file : m_managedFiles) {
        if (file->status() == ManagedFile::Ready && !file->fileId().isEmpty()) {
            userAttachments.append(file->fileId());
        }
    }

    // On envoie la requête avec l'historique ET la pièce jointe
    m_interlocutor->sendRequest(m_messages, userAttachments);

    // 4. Gérer la logique de l'indicateur d'attente de réponse
    setWaitingForReply(true);

    // Ajouter un message spécial "indicateur d'attente"
    ChatMessage typingIndicator(false, "", QDateTime::currentDateTime(), 0, 0, "assistant");
    typingIndicator.isTypingIndicator = true;
    addMessage(typingIndicator);
}

void ChatModel::handleInterlocutorResponse(const QJsonObject &response)
{
    // 1. Si la requête était une demande de curation de la mémoire ancienne,
    // On va la traiter ici.
    if (m_isWaitingForCurationResponse) {
        qDebug() << "Step 3/4 Curation response received.";
        m_isWaitingForCurationResponse = false;

        // On supprime le fichier temporaire de la mémoire vive
        m_interlocutor->deleteFile(m_liveMemoryFileIdForCuration);
        m_liveMemoryFileIdForCuration.clear();

        QString newSummary;
        // Extraire le résumé de la réponse
        if (response.contains("choices") && response["choices"].isArray()) {
            QJsonArray choicesArray = response["choices"].toArray();
            if (!choicesArray.isEmpty()) {
                newSummary = choicesArray[0].toObject()["message"].toObject()["content"].toString();
            }
        }

        if (!newSummary.isEmpty()) {
            qDebug() << "Step 4/4: Uploading new ancient memory...";
            saveOlderMemory(newSummary);
            // On stocke l'ID de l'ancienne mémoire pour la supprimer après le nouvel upload
            InterlocutorConfig *config = findCurrentConfig();
            if (config && !config->ancientMemoryFileId().isEmpty()) {
                m_oldAncientMemoryFileIdToDelete = config->ancientMemoryFileId();
            }
            m_interlocutor->uploadFile(newSummary.toUtf8(), "curation_ancient_memory");
            qDebug() << "Older memory successfully updated.";
            emit curationFinished(true);
        } else {
            qWarning() << "Curation failed: received empty summary.";
            emit curationFinished(false);
        }

        // Réinitialiser les flags
        m_isWaitingForCurationResponse = false;
        m_isCurationInProgress = false;
        return; // Le traitement de cette réponse est terminé.
    }
    // --- FIN DE LA LOGIQUE DE CURATION ---

    // 2. Retirer l'indicateur d'attente de réponse, qui est le dernier message de la liste
    if (!m_messages.isEmpty() && m_messages.last().isTypingIndicator) {
        beginRemoveRows(QModelIndex(), m_messages.count() - 1, m_messages.count() - 1);
        m_messages.removeLast();
        endRemoveRows();
    }

    // 3. Extraire le contenu du message de l'IA de la réponse (structure OpenAI-like)
    QString aiResponseText;
    int promptTokens = 0;
    int completionTokens = 0;
    int totalTokensForThisExchange = 0;

    if (response.contains("choices") && response["choices"].isArray()) {
        QJsonArray choicesArray = response["choices"].toArray();
        if (!choicesArray.isEmpty() && choicesArray[0].isObject()) {
            QJsonObject firstChoice = choicesArray[0].toObject();
            if (firstChoice.contains("message") && firstChoice["message"].isObject()) {
                QJsonObject message = firstChoice["message"].toObject();
                qDebug()<<"Objet JSON reçu:"<<message;
                if (message.contains("content")) {
                    aiResponseText = message["content"].toString();
                }
            }
        }
    }

    if (response.contains("usage") && response["usage"].isObject()) {
        QJsonObject usage = response["usage"].toObject();
        promptTokens = usage["prompt_tokens"].toInt(0);
        completionTokens = usage["completion_tokens"].toInt(0);
        totalTokensForThisExchange = usage["total_tokens"].toInt(0);
    }

    // 4. Mettre à jour la taille de la Mémoire Vive pour la curation
    // La nouvelle taille est simplement le `prompt_tokens` (qui inclut l'historique) + `completion_tokens`.
    int newLiveMemorySize = promptTokens + completionTokens;
    if (m_liveMemoryTokens != newLiveMemorySize) {
        m_liveMemoryTokens = newLiveMemorySize;
        emit liveMemoryTokensChanged();
        qDebug() << "Live Memory size is now:" << m_liveMemoryTokens << "tokens.";
    }

    // 5. Mettre à jour le coût cumulatif pour l'utilisateur
    // On AJOUTE le coût de cet échange au total.
    m_cumulativeTokenCost += totalTokensForThisExchange;
    emit cumulativeTokenCostChanged();
    qDebug() << "Cumulative token cost is now:" << m_cumulativeTokenCost;

    setWaitingForReply(false); // Fin de l'attente

    // Ajouter le message de l'IA au modèle, en stockant les tokens de cet échange
    ChatMessage aiMessage(false,
                          aiResponseText,
                          QDateTime::currentDateTime(),
                          promptTokens,
                          completionTokens,
                          "assistant");
    addMessage(aiMessage);

    // 6. Vérifier si il y a nécessité de faire une curation de la mémoire ancienne.
    // La curation est basée sur la taille de la mémoire vive
    checkCurationThreshold();
}

void ChatModel::handleInterlocutorError(const QString &error)
{
    // Retirer l'indicateur "attente de réponse" même en cas d'erreur
    if (!m_messages.isEmpty() && m_messages.last().isTypingIndicator) {
        beginRemoveRows(QModelIndex(), m_messages.count() - 1, m_messages.count() - 1);
        m_messages.removeLast();
        endRemoveRows();
    }
    setWaitingForReply(false);

    qWarning() << "Interlocutor Error:" << error;
    emit chatError("Error from AI: " + error);
    // Optionnel: ajouter un message d'erreur visible dans le chat
    ChatMessage errorMessage(false, "ERROR: " + error, QDateTime::currentDateTime(), 0, 0, "system");
    addMessage(errorMessage);
    updateLiveMemoryEstimate();
}

void ChatModel::addMessage(const ChatMessage &message)
{
    beginInsertRows(QModelIndex(), m_messages.count(), m_messages.count());
    m_messages.append(message);
    endInsertRows();

    // Persister le message dans le fichier jsonl si ce n'est pas un typing indicator
    if (!message.isTypingIndicator && !m_currentChatFilePath.isEmpty()) {
        QFile file(m_currentChatFilePath);
        if (file.open(QFile::Append | QFile::Text)) {
            QTextStream stream(&file);
            stream << QJsonDocument(message.toJsonObject()).toJson(QJsonDocument::Compact) << "\n";
            file.close();
        } else {
            qWarning() << "Failed to open chat file for appending:" << m_currentChatFilePath;
        }
    }
    emit chatMessageAdded(message);
}

void ChatModel::setWaitingForReply(bool waiting) {
    if (m_isWaitingForReply != waiting) {
        m_isWaitingForReply = waiting;
        emit isWaitingForReplyChanged();
    }
}

void ChatModel::loadChat(const QString &filePath)
{
    if (m_currentChatFilePath == filePath && !filePath.isEmpty()) {
        qDebug() << "Chat file already loaded or path is the same:" << filePath;
        return;
    }

    // Sauvegarder le chat précédent si un fichier était ouvert
    saveChat();

    beginResetModel(); // Réinitialiser le modèle pour le chargement d'un nouveau chat
    m_messages.clear();
    m_liveMemoryTokens = 0;    // Réinitialiser
    m_cumulativeTokenCost = 0; // Réinitialiser
    // Vider les anciennes listes
    qDeleteAll(m_managedFiles);
    m_managedFiles.clear();
    m_messages.clear();

    if (filePath.isEmpty()) {
        qDebug() << "No file path provided to load chat. Starting with an empty chat.";
        m_currentChatFilePath = ""; // S'assurer que le chemin est vide
        endResetModel();
        emit currentChatFilePathChanged();
        emit liveMemoryTokensChanged();
        return;
    }

    QFile file(filePath);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        qWarning() << "Failed to open chat file for reading:" << filePath;
        // Créer un nouveau fichier si inexistant
        if (!file.exists()) {
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
    while (!stream.atEnd()) {
        QByteArray line = stream.readLine().toUtf8();
        QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isNull() && doc.isObject()) {
            ChatMessage msg = ChatMessage::fromJsonObject(doc.object());
            m_messages.append(msg);
            m_cumulativeTokenCost += msg.promptTokens() + msg.completionTokens();
        } else {
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
    // Le mécanisme de `addMessage` sauvegarde déjà chaque message individuellement.
    // Cette méthode `saveChat` serait utile si on voulait réécrire tout le fichier
    // après une modification ou une curation, ou nous travaiilons avec un buffer en RAM.
    // Pour l'instant, elle n'est pas strictement nécessaire avec l'approche jsonl en append.
    // On la laisse vide pour l'instant ou on l'utilise pour une future réécriture complète.
    qDebug() << "ChatModel::saveChat() called. Doing nothing. Messages are saved incrementally.";
}

void ChatModel::clearChat()
{
    if (m_messages.isEmpty())
        return;

    beginRemoveRows(QModelIndex(), 0, m_messages.count() - 1);
    m_messages.clear();
    endRemoveRows();

    // Effacer le fichier local associé
    if (!m_currentChatFilePath.isEmpty()) {
        QFile file(m_currentChatFilePath);
        if (file.exists()) {
            if (file.remove()) {
                qDebug() << "Chat file removed:" << m_currentChatFilePath;
            } else {
                qWarning() << "Failed to remove chat file:" << m_currentChatFilePath;
            }
        }
    }
    m_liveMemoryTokens = 0;
    emit liveMemoryTokensChanged();
    emit chatError("Chat cleared.");
}

void ChatModel::setCurrentChatFilePath(const QString &path)
{
    if (m_currentChatFilePath == path)
        return;
    // La logique de loadChat gère déjà la sauvegarde de l'ancien chat et le changement de chemin
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
    for (const auto &msg : m_messages) {
        estimatedTokens += msg.text().length() / 4;
    }

    if (m_liveMemoryTokens != estimatedTokens) {
        m_liveMemoryTokens = estimatedTokens;
        emit liveMemoryTokensChanged();
        qDebug() << "Estimated Live Memory on load:" << m_liveMemoryTokens << "tokens.";
    }
}

void ChatModel::checkCurationThreshold()
{
    qDebug()<<"There are currently"<<m_liveMemoryTokens<<"in the live memory";
    if (m_liveMemoryTokens >= CURATION_TRIGGER_TOKENS && !m_isCurationInProgress) {
        qDebug() << "Curation threshold reached! Live memory size:" << m_liveMemoryTokens;
        emit curationNeeded();
        triggerCuration();
    }
}

void ChatModel::setInterlocutor(Interlocutor *interlocutor)
{
    // Déconnecter l'ancien interlocuteur s'il existe
    if (m_interlocutor) {
        disconnect(m_interlocutor,
                   &Interlocutor::responseReceived,
                   this,
                   &ChatModel::handleInterlocutorResponse);
        disconnect(m_interlocutor,
                   &Interlocutor::errorOccurred,
                   this,
                   &ChatModel::handleInterlocutorError);
        disconnect(m_interlocutor, &Interlocutor::fileUploaded, this, &ChatModel::onFileUploaded);
        disconnect(m_interlocutor,
                   &Interlocutor::fileUploadFailed,
                   this,
                   &ChatModel::onFileUploadFailed);
        disconnect(m_interlocutor, &Interlocutor::fileDeleted, this, &ChatModel::onFileDeleted);
    }

    m_interlocutor = interlocutor;

    // Connecter le nouvel interlocuteur s'il n'est pas nul
    if (m_interlocutor) {
        connect(m_interlocutor,
                &Interlocutor::responseReceived,
                this,
                &ChatModel::handleInterlocutorResponse);
        connect(m_interlocutor,
                &Interlocutor::errorOccurred,
                this,
                &ChatModel::handleInterlocutorError);
        connect(m_interlocutor, &Interlocutor::fileUploaded, this, &ChatModel::onFileUploaded);
        connect(m_interlocutor,
                &Interlocutor::fileUploadFailed,
                this,
                &ChatModel::onFileUploadFailed);
        connect(m_interlocutor, &Interlocutor::fileDeleted, this, &ChatModel::onFileDeleted);
    }
}

void ChatModel::rewriteChatFile()
{
    if (m_currentChatFilePath.isEmpty()) {
        return; // Pas de fichier à réécrire
    }

    QFile file(m_currentChatFilePath);
    // On ouvre en mode écriture seule, ce qui tronque (efface) le fichier existant.
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        qWarning() << "Failed to open chat file for rewriting:" << m_currentChatFilePath;
        return;
    }

    QTextStream stream(&file);
    for (const ChatMessage &message : m_messages) {
        stream << QJsonDocument(message.toJsonObject()).toJson(QJsonDocument::Compact) << "\n";
    }
    file.close();
    qDebug() << "Chat file rewritten successfully:" << m_currentChatFilePath;
}

void ChatModel::triggerCuration()
{
    if (m_isCurationInProgress)
        return; // Sécurité supplémentaire

    qDebug() << "Starting curation process...";
    m_isCurationInProgress = true;

    // --- Phase 1: Prélèvement des messages de la mémoire vive ---
    QList<ChatMessage> messagesToCurate;
    int tokensToCull = 0;
    int numMessagesToRemove = 0;

    // On retire les messages jusqu'à redescendre sous le seuil de base
    while (m_liveMemoryTokens > BASE_LIVE_MEMORY_TOKENS && !m_messages.isEmpty()) {
        ChatMessage msg = m_messages.first();
        messagesToCurate.append(msg);

        int msgTokens = msg.promptTokens() + msg.completionTokens();
        // Estimation si les tokens sont à 0
        if (msgTokens == 0)
            msgTokens = msg.text().length() / 4;

        m_liveMemoryTokens -= msgTokens;
        m_messages.removeFirst();
        numMessagesToRemove++;
    }

    if (numMessagesToRemove > 0) {
        qDebug() << "Culling" << numMessagesToRemove << "messages from live memory.";
        beginRemoveRows(QModelIndex(), 0, numMessagesToRemove - 1);
        endRemoveRows();
        rewriteChatFile();              // On met à jour le fichier de la mémoire vive
        emit liveMemoryTokensChanged(); // On notifie l'UI du nouveau total de tokens
    } else {
        qWarning() << "Curation triggered, but no messages to cull. Aborting.";
        m_isCurationInProgress = false;
        return;
    }

    // --- Phase 2: Préparation de la requête de résumé ---
    QString olderMemory = loadOlderMemory();
    QString conversationToSummarize;
    for (const auto &msg : messagesToCurate) { /* ... */
    }

    qDebug() << "Step 1/4: Uploading live memory for curation...";
    // On connecte un slot spécifique pour cette étape
    qDebug() << "Step 1/4: Uploading live memory for curation...";
    // On donne un "purpose" spécifique pour que le slot `onFileUploaded` sache quoi faire
    m_interlocutor->uploadFile(conversationToSummarize.toUtf8(), "curation_live_memory");
}

InterlocutorConfig *ChatModel::findCurrentConfig() {
    QObject* parentObj = parent();
    if (parentObj) {
        ChatManager* chatManager = qobject_cast<ChatManager*>(parentObj);
        if (chatManager) {
            return chatManager->findCurrentConfig();
        }
    }
    return nullptr;
}

void ChatModel::onLiveMemoryUploadedForCuration(const QString &fileId)
{
    qDebug() << "Step 2/4: Live memory uploaded (ID:" << fileId << "). Sending curation request...";
    m_liveMemoryFileIdForCuration = fileId; // On stocke l'ID pour pouvoir le supprimer plus tard

    // Construire le prompt de curation
    QString olderMemory = loadOlderMemory();
    QString curationPrompt
        = "You are a memory assistant. Your task is to update a long-term memory summary."
          "The full transcript of the recent conversation is in the attached file. "
          "Please read the existing summary below, then read the attached file, and finally "
          "produce a new, updated summary that integrates the key information from the "
          "transcript.\n\n"
          "--- EXISTING SUMMARY ---\n"
          + (olderMemory.isEmpty() ? "None." : olderMemory);

    // On envoie la requête de curation en attachant le fichier de mémoire vive
    m_isWaitingForCurationResponse = true;
    QList<ChatMessage> curationRequestHistory;
    curationRequestHistory.append(
        ChatMessage(true, curationPrompt, QDateTime::currentDateTime(), 0, 0, "user"));
    m_interlocutor->sendRequest(curationRequestHistory, {fileId});
}

// --- Implémentation des nouvelles fonctions utilitaires ---

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
    QString memoryFilePath = getOlderMemoryFilePath();
    if (memoryFilePath.isEmpty())
        return "";

    QFile file(memoryFilePath);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        return ""; // Pas encore de fichier de mémoire, c'est normal au début
    }
    QTextStream in(&file);
    return in.readAll();
}

void ChatModel::saveOlderMemory(const QString &content)
{
    QString memoryFilePath = getOlderMemoryFilePath();
    if (memoryFilePath.isEmpty()) {
        qWarning() << "Cannot save older memory: no current chat file path set.";
        return;
    }

    QFile file(memoryFilePath);
    if (!file.open(QFile::WriteOnly | QFile::Text | QFile::Truncate)) {
        qWarning() << "Failed to open older memory file for writing:" << memoryFilePath;
        return;
    }
    QTextStream out(&file);
    out << content;
}

void ChatModel::onNewAncientMemoryUploaded(const QString &newFileId)
{
    qDebug() << "New ancient memory uploaded (ID:" << newFileId << "). Updating config...";

    // Mettre à jour la config avec le nouvel ID
    InterlocutorConfig *config = findCurrentConfig();
    if (config) {
        config->setAncientMemoryFileId(newFileId);
        // Il faut une façon de dire au ChatManager de sauvegarder
        // chatManager->saveInterlocutorsToDisk(); // Idéalement via un signal
    }

    // Si une ancienne mémoire existait, on la supprime
    if (!m_oldAncientMemoryFileIdToDelete.isEmpty()) {
        qDebug() << "Deleting old ancient memory file (ID:" << m_oldAncientMemoryFileIdToDelete
                 << ")";
        m_interlocutor->deleteFile(m_oldAncientMemoryFileIdToDelete);
    } else {
        m_isCurationInProgress = false; // Fin du processus
    }
}

void ChatModel::onOldAncientMemoryDeleted(const QString &fileId, bool success)
{
    qDebug() << "Old ancient memory file deletion result:" << success;
    m_oldAncientMemoryFileIdToDelete.clear();
    m_isCurationInProgress = false; // C'est la toute fin du processus de curation !
}

void ChatModel::onCurationUploadFailed(const QString &error)
{
    qWarning() << "Curation process failed at upload stage:" << error;
    // Gérer l'erreur : réinitialiser les flags, notifier l'utilisateur...
    m_isCurationInProgress = false;
    m_isWaitingForCurationResponse = false;
}

QList<QObject *> ChatModel::managedFiles() const
{
    QList<QObject *> list;
    for (ManagedFile *file : m_managedFiles) {
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
    if (!file.open(QIODevice::ReadOnly)) {
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
    m_interlocutor->uploadFile(content, "user_attachment");
}

void ChatModel::deleteUserFile(int index)
{
    if (index < 0 || index >= m_managedFiles.size())
        return;

    ManagedFile *fileToDelete = m_managedFiles.at(index);
    if (!fileToDelete->fileId().isEmpty()) {
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
    if (purpose == "user_attachment") {
        // C'est un fichier uploadé par l'utilisateur.
        qDebug() << "User file uploaded successfully. ID:" << fileId;
        for (int i = m_managedFiles.size() - 1; i >= 0; --i) {
            if (m_managedFiles[i]->status() == ManagedFile::Uploading) {
                m_managedFiles[i]->setFileId(fileId);
                m_managedFiles[i]->setStatus(ManagedFile::Ready);
                saveManagedFiles(); // SAUVEGARDER la liste après un upload réussi !
                return;             // On a trouvé et mis à jour le bon fichier
            }
        }
    } else if (purpose == "curation_live_memory") {
        // Étape 1 de la curation : la mémoire vive est uploadée
        onLiveMemoryUploadedForCuration(fileId); // On appelle une fonction privée pour la suite
    } else if (purpose == "curation_ancient_memory") {
        // Étape finale de la curation : la nouvelle mémoire ancienne est uploadée
        onNewAncientMemoryUploaded(fileId); // On appelle une autre fonction privée pour la suite
    }
}

void ChatModel::onFileDeleted(const QString &fileId, bool success)
{
    // Ici, c'est principalement pour la fin de la curation
    if (!m_oldAncientMemoryFileIdToDelete.isEmpty() && fileId == m_oldAncientMemoryFileIdToDelete) {
        onOldAncientMemoryDeleted(fileId, success);
    }
    // On pourrait aussi ajouter une logique si la suppression d'un fichier utilisateur échoue
}

void ChatModel::onFileUploadFailed(const QString &error)
{
    // On doit savoir quel upload a échoué. On peut se baser sur les flags d'état.
    if (m_isCurationInProgress) {
        onCurationUploadFailed(error);
    } else {
        // C'était probablement un fichier utilisateur
        qWarning() << "User file upload failed:" << error;
        // On cherche le dernier fichier en "Uploading" pour le passer en "Error"
        for (int i = m_managedFiles.size() - 1; i >= 0; --i) {
            if (m_managedFiles[i]->status() == ManagedFile::Uploading) {
                m_managedFiles[i]->setStatus(ManagedFile::Error);
                break;
            }
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

    for (const QJsonValue &value : fileArray) {
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
    for (const ManagedFile *file : m_managedFiles) {
        // On ne sauvegarde que les fichiers qui sont prêts et uploadés
        if (file->status() == ManagedFile::Ready) {
            fileArray.append(file->toJsonObject());
        }
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Could not open file list for writing:" << path;
        return;
    }
    file.write(QJsonDocument(fileArray).toJson(QJsonDocument::Indented));
}

// End Source File: ChatModel.cpp
