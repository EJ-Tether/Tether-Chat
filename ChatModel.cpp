// Begin Source File : ChatModel.cpp
#include "ChatModel.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QFileInfo>

ChatModel::ChatModel(QObject *parent)
    : QAbstractListModel(parent),
    m_interlocutor(nullptr),
    m_totalTokens(0)
{
    if (m_interlocutor) {
        connect(m_interlocutor, &Interlocutor::responseReceived, this, &ChatModel::handleInterlocutorResponse);
        connect(m_interlocutor, &Interlocutor::errorOccurred, this, &ChatModel::handleInterlocutorError);
    }
}

ChatModel::~ChatModel() {
    saveChat(); // Sauvegarder la conversation à la fermeture si nécessaire
}

int ChatModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid())
        return 0;
    return m_messages.count();
}

QVariant ChatModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_messages.count())
        return QVariant();

    const ChatMessage &message = m_messages.at(index.row());
    switch (role) {
    case IsLocalMessageRole: return message.isLocalMessage();
    case TextRole: return message.text();
    case TimestampRole: return message.timestamp();
    case PromptTokensRole: return message.promptTokens();
    case CompletionTokensRole: return message.completionTokens();
    case RoleRole: return message.role();
    }
    return QVariant();
}

QHash<int, QByteArray> ChatModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[IsLocalMessageRole] = "isLocalMessage";
    roles[TextRole] = "text";
    roles[TimestampRole] = "timestamp";
    roles[PromptTokensRole] = "promptTokens";
    roles[CompletionTokensRole] = "completionTokens";
    roles[RoleRole] = "role";
    return roles;
}

void ChatModel::sendMessage(const QString &messageText) {
    if (!m_interlocutor) {
        emit chatError("Interlocutor is not set.");
        return;
    }

    // 1. Ajouter le message de l'utilisateur au modèle
    ChatMessage userMessage(true, messageText, QDateTime::currentDateTime(), 0, 0, "user");
    addMessage(userMessage);

    // TODO: Estimer les tokens du prompt pour le message utilisateur avant d'envoyer
    // Pour l'instant, on laisse à 0 et on se base sur le retour de l'API pour les tokens réellement consommés.
    // Ou on pourrait faire une estimation simple: userMessage.setPromptTokens(messageText.length() / 4);

    // 2. Envoyer le message à l'interlocuteur
    m_interlocutor->sendRequest(messageText);
}

void ChatModel::handleInterlocutorResponse(const QJsonObject &response) {
    // --- NOUVELLE LOGIQUE DE TRI ---
    // On vérifie si cette réponse concerne la curation ou un chat normal.
    if (m_isWaitingForCurationResponse) {
        qDebug() << "Curation response received.";
        QString summaryText;

        // Extraire le résumé de la réponse
        if (response.contains("choices") && response["choices"].isArray()) {
            QJsonArray choicesArray = response["choices"].toArray();
            if (!choicesArray.isEmpty()) {
                summaryText = choicesArray[0].toObject()["message"].toObject()["content"].toString();
            }
        }

        if (!summaryText.isEmpty()) {
            saveOlderMemory(summaryText);
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
    // Extraire le contenu du message de l'IA de la réponse (structure OpenAI-like)
    QString aiResponseText;
    int promptTokens = 0;
    int completionTokens = 0;

    if (response.contains("choices") && response["choices"].isArray()) {
        QJsonArray choicesArray = response["choices"].toArray();
        if (!choicesArray.isEmpty() && choicesArray[0].isObject()) {
            QJsonObject firstChoice = choicesArray[0].toObject();
            if (firstChoice.contains("message") && firstChoice["message"].isObject()) {
                QJsonObject message = firstChoice["message"].toObject();
                if (message.contains("content")) {
                    aiResponseText = message["content"].toString();
                }
            }
        }
    }

    // Extraire l'usage des tokens si disponible (simulé par DummyInterlocutor)
    if (response.contains("usage") && response["usage"].isObject()) {
        QJsonObject usage = response["usage"].toObject();
        promptTokens = usage["prompt_tokens"].toInt(0); // Tokens de la requête totale
        completionTokens = usage["completion_tokens"].toInt(0); // Tokens de la réponse
    }

    // Mise à jour des tokens du dernier message UTILISATEUR (le prompt qui a généré la réponse)
    // C'est un peu délicat car l'API retourne l'usage total du prompt, y compris le system message.
    // Pour une gestion plus précise, il faudrait stocker le contexte envoyé à l'API.
    // Pour le moment, on assigne ces tokens au dernier message utilisateur.
    if (!m_messages.isEmpty()) {
        ChatMessage &lastUserMessage = m_messages.last();
        if (lastUserMessage.isLocalMessage()) { // Assurez-vous que c'est bien le message de l'utilisateur
            lastUserMessage.setPromptTokens(promptTokens); // Tokens envoyés pour la requête
            // On a changé le nombre de tokens émis, on doit le signaler au QML par un signal:
            int lastIndex = m_messages.count() - 1;
            QModelIndex modelIndex = index(lastIndex);
            emit dataChanged(modelIndex, modelIndex, {PromptTokensRole});
        }
    }


    // Ajouter le message de l'IA au modèle
    ChatMessage aiMessage(false, aiResponseText, QDateTime::currentDateTime(), 0, completionTokens, "assistant");
    addMessage(aiMessage); // Le promptTokens est 0 pour le message AI lui-même, completionTokens est la réponse
    updateTokenCount();
    checkCurationThreshold();
}

void ChatModel::handleInterlocutorError(const QString &error) {
    qWarning() << "Interlocutor Error:" << error;
    emit chatError("Error from AI: " + error);
    // Optionnel: ajouter un message d'erreur visible dans le chat
    ChatMessage errorMessage(false, "ERROR: " + error, QDateTime::currentDateTime(), 0, 0, "system");
    addMessage(errorMessage);
    updateTokenCount();
}


void ChatModel::addMessage(const ChatMessage &message) {
    beginInsertRows(QModelIndex(), m_messages.count(), m_messages.count());
    m_messages.append(message);
    endInsertRows();

    // Persister le message dans le fichier jsonl
    if (!m_currentChatFilePath.isEmpty()) {
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
    updateTokenCount(); // Met à jour le total des tokens après l'ajout
}


void ChatModel::loadChat(const QString &filePath) {
    if (m_currentChatFilePath == filePath && !filePath.isEmpty()) {
        qDebug() << "Chat file already loaded or path is the same:" << filePath;
        return;
    }

    // Sauvegarder le chat précédent si un fichier était ouvert
    saveChat();

    beginResetModel(); // Réinitialiser le modèle pour le chargement d'un nouveau chat
    m_messages.clear();
    m_totalTokens = 0; // Réinitialiser le compteur de tokens

    if (filePath.isEmpty()) {
        qDebug() << "No file path provided to load chat. Starting with an empty chat.";
        m_currentChatFilePath = ""; // S'assurer que le chemin est vide
        endResetModel();
        emit currentChatFilePathChanged();
        emit totalTokensChanged();
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
        emit totalTokensChanged();
        return;
    }

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        QByteArray line = stream.readLine().toUtf8();
        QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isNull() && doc.isObject()) {
            ChatMessage msg = ChatMessage::fromJsonObject(doc.object());
            m_messages.append(msg);
        } else {
            qWarning() << "Skipping malformed JSON line in chat file:" << filePath;
        }
    }
    file.close();

    m_currentChatFilePath = filePath;
    updateTokenCount(); // Recalculer après chargement
    endResetModel();

    emit currentChatFilePathChanged();
    emit totalTokensChanged();
    qDebug() << "Chat loaded from" << filePath << "with" << m_messages.count() << "messages and" << m_totalTokens << "tokens.";
    checkCurationThreshold();
}


void ChatModel::saveChat() {
    // Le mécanisme de `addMessage` sauvegarde déjà chaque message individuellement.
    // Cette méthode `saveChat` serait utile si on voulait réécrire tout le fichier
    // après une modification ou une curation, ou nous travaiilons avec un buffer en RAM.
    // Pour l'instant, elle n'est pas strictement nécessaire avec l'approche jsonl en append.
    // On la laisse vide pour l'instant ou on l'utilise pour une future réécriture complète.
    qDebug() << "ChatModel::saveChat() called. Doing nothing. Messages are saved incrementally.";
}

void ChatModel::clearChat() {
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
    m_totalTokens = 0;
    emit totalTokensChanged();
    emit chatError("Chat cleared.");
}


void ChatModel::setCurrentChatFilePath(const QString &path) {
    if (m_currentChatFilePath == path)
        return;
    // La logique de loadChat gère déjà la sauvegarde de l'ancien chat et le changement de chemin
    loadChat(path); // Recharge le chat avec le nouveau chemin
}

void ChatModel::updateTokenCount() {
    int newTotalTokens = 0;
    for (const auto &msg : m_messages) {
        newTotalTokens += msg.promptTokens();
        newTotalTokens += msg.completionTokens();
        // Une estimation simple si les tokens ne sont pas encore définis:
        // newTotalTokens += msg.text().length() / 4; // Environ 4 caractères par token
    }
    if (m_totalTokens != newTotalTokens) {
        m_totalTokens = newTotalTokens;
        emit totalTokensChanged();
        qDebug() << "Current total tokens:" << m_totalTokens;
    }
}

void ChatModel::checkCurationThreshold() {
    if (m_totalTokens >= CURATION_TRIGGER_TOKENS) {
        qDebug() << "Curation threshold reached! Total tokens:" << m_totalTokens;
        emit curationNeeded(); // Signale que la curation doit être lancée
        triggerCuration(); // Appel du placeholder de la curation
    }
}

void ChatModel::setInterlocutor(Interlocutor *interlocutor) {
    // Déconnecter l'ancien interlocuteur s'il existe
    if (m_interlocutor) {
        disconnect(m_interlocutor, &Interlocutor::responseReceived, this, &ChatModel::handleInterlocutorResponse);
        disconnect(m_interlocutor, &Interlocutor::errorOccurred, this, &ChatModel::handleInterlocutorError);
    }

    m_interlocutor = interlocutor;

    // Connecter le nouvel interlocuteur s'il n'est pas nul
    if (m_interlocutor) {
        connect(m_interlocutor, &Interlocutor::responseReceived, this, &ChatModel::handleInterlocutorResponse);
        connect(m_interlocutor, &Interlocutor::errorOccurred, this, &ChatModel::handleInterlocutorError);
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

void ChatModel::triggerCuration() {
    if (m_isCurationInProgress) return; // Sécurité supplémentaire

    qDebug() << "Starting curation process...";
    m_isCurationInProgress = true;

    // --- Phase 1: Prélèvement des messages de la mémoire vive ---
    QList<ChatMessage> messagesToCurate;
    int tokensToCull = 0;
    int numMessagesToRemove = 0;

    // On retire les messages jusqu'à redescendre sous le seuil de base
    while (m_totalTokens > BASE_LIVE_MEMORY_TOKENS && !m_messages.isEmpty()) {
        ChatMessage msg = m_messages.first();
        messagesToCurate.append(msg);

        int msgTokens = msg.promptTokens() + msg.completionTokens();
        // Estimation si les tokens sont à 0
        if (msgTokens == 0) msgTokens = msg.text().length() / 4;

        m_totalTokens -= msgTokens;
        m_messages.removeFirst();
        numMessagesToRemove++;
    }

    if (numMessagesToRemove > 0) {
        qDebug() << "Culling" << numMessagesToRemove << "messages from live memory.";
        beginRemoveRows(QModelIndex(), 0, numMessagesToRemove - 1);
        endRemoveRows();
        rewriteChatFile(); // On met à jour le fichier de la mémoire vive
        emit totalTokensChanged(); // On notifie l'UI du nouveau total de tokens
    } else {
        qWarning() << "Curation triggered, but no messages to cull. Aborting.";
        m_isCurationInProgress = false;
        return;
    }

    // --- Phase 2: Préparation de la requête de résumé ---
    QString olderMemory = loadOlderMemory();
    QString conversationToSummarize;

    // On transforme la liste des messages en un dialogue textuel
    for (const auto& msg : messagesToCurate) {
        QString role = msg.isLocalMessage() ? "User" : "Assistant";
        conversationToSummarize += role + ": " + msg.text() + "\n\n";
    }

    // On construit le prompt final
    QString curationPrompt = "You are a memory assistant. Your task is to update a long-term memory summary."
                             "Below is the existing summary, followed by the most recent conversation transcript that needs to be integrated."
                             "Rewrite the summary to incorporate the new key information, facts, user preferences, and important events from the transcript."
                             "The new summary should be a concise, coherent narrative. Discard trivial details.\n\n"
                             "--- EXISTING SUMMARY ---\n" +
                             (olderMemory.isEmpty() ? "None." : olderMemory) +
                             "\n\n--- RECENT TRANSCRIPT TO INTEGRATE ---\n" +
                             conversationToSummarize;

    // --- Phase 3: Appel asynchrone à l'IA ---
    qDebug() << "Sending request for curation summary...";
    m_isWaitingForCurationResponse = true;
    m_interlocutor->sendRequest(curationPrompt);
}

// --- Implémentation des nouvelles fonctions utilitaires ---

QString ChatModel::getOlderMemoryFilePath() const {
    if (m_currentChatFilePath.isEmpty()) return "";
    // On base le nom du fichier de mémoire sur celui du chat
    // ex: "default_chat.jsonl" -> "default_chat_memory.txt"
    QFileInfo fileInfo(m_currentChatFilePath);
    return fileInfo.path() + "/" + fileInfo.baseName() + "_memory.txt";
}

QString ChatModel::loadOlderMemory() {
    QString memoryFilePath = getOlderMemoryFilePath();
    if (memoryFilePath.isEmpty()) return "";

    QFile file(memoryFilePath);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        return ""; // Pas encore de fichier de mémoire, c'est normal au début
    }
    QTextStream in(&file);
    return in.readAll();
}

void ChatModel::saveOlderMemory(const QString &content) {
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
// End Source File: ChatModel.cpp
