#include "ChatModel.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths> // Pour les chemins par défaut

ChatModel::ChatModel(Interlocutor *interlocutor, QObject *parent)
    : QAbstractListModel(parent),
    m_interlocutor(interlocutor),
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
            // Aucun signal besoin ici car on ne modifie pas le texte
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

void ChatModel::triggerCuration() {
    // TODO: Implémenter la logique de curation
    // 1. Identifier les messages les plus anciens à curer
    // 2. Les envoyer à l'IA pour un résumé
    // 3. Recevoir le résumé et l'ajouter au "journal de mémoire" (older memory)
    // 4. Supprimer les messages curés de m_messages et du fichier jsonl (nécessite une réécriture du fichier)
    // 5. Mettre à jour m_totalTokens
    qDebug() << "Placeholder: Triggering curation process...";

    // Pour l'instant, comme simple simulation:
    // On supprime les premiers messages pour redescendre sous le seuil
    // Ceci est une SIMULATION TRÈS BASIQUE, la vraie curation est plus complexe.
    if (m_messages.count() > 5) { // Si plus de 5 messages, on en supprime quelques-uns
        int numToRemove = m_messages.count() / 3; // Par exemple, supprimer un tiers des messages
        if (numToRemove > 0) {
            beginRemoveRows(QModelIndex(), 0, numToRemove - 1);
            for (int i = 0; i < numToRemove; ++i) {
                m_messages.removeFirst();
            }
            endRemoveRows();
            qDebug() << numToRemove << "messages removed during dummy curation.";
            // Après suppression, il faudrait réécrire le fichier jsonl,
            // ou marquer ces messages comme "curés" sans les supprimer du fichier historique.
            // Pour l'instant, on se concentre sur le modèle en mémoire.
            updateTokenCount();
            // Après une vraie curation, il faudrait sauvegarder la mémoire curée
            // dans un fichier séparé et mettre à jour le fichier de chat courant.
        }
    }
}

// Enregistrer le type ChatMessage pour qu'il puisse être utilisé avec QVariant
// QML_DECLARE_TYPE(ChatMessage) // Ceci est utilisé pour les types qui sont des QObject, pas pour Q_GADGETs
