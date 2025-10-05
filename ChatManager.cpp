#include "ChatManager.h"
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

ChatManager::ChatManager(QObject *parent)
    : QObject(parent), m_chatModel(new ChatModel(this))
{
    // Définir le chemin de stockage des chats
    m_chatFilesPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/TetherChats";
    QDir dir(m_chatFilesPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    loadInterlocutors();

    // Par défaut, activer le premier interlocuteur trouvé
    if (!m_interlocutors.isEmpty()) {
        switchToInterlocutor(m_interlocutors.firstKey());
    }
}

ChatManager::~ChatManager() {
    // Le QObject parent s'occupe de détruire les enfants (m_chatModel, m_interlocutors)
}

void ChatManager::loadInterlocutors() {
    // Pour l'instant, on charge juste le Dummy en dur.
    // Plus tard, on chargera tous les Interlocutor connus depuis l'objet Settings (qui reflète l'onglet Configuration)
    auto dummy = new DummyInterlocutor("Dummy", this);
    m_interlocutors.insert("Dummy", dummy);

    // Exemple de comment on ajouterai un vrai interlocuteur
    // auto openai = new OpenAIInterlocutor("ChatGPT-4o", this);
    // openai->setApiKey("MA_CLÉ_API");
    // m_interlocutors.insert("ChatGPT-4o", openai);

    emit interlocutorNamesChanged();
}

QStringList ChatManager::interlocutorNames() const {
    return m_interlocutors.keys();
}

void ChatManager::switchToInterlocutor(const QString &name) {
    if (!m_interlocutors.contains(name) || m_activeInterlocutorName == name) {
        return; // Inconnu ou déjà actif
    }

    qDebug() << "Switching to interlocutor:" << name;

    Interlocutor* interlocutor = m_interlocutors.value(name);
    m_chatModel->setInterlocutor(interlocutor);

    // Charger le fichier de chat correspondant
    QString chatFilePath = m_chatFilesPath + "/" + name + ".jsonl";
    m_chatModel->loadChat(chatFilePath);

    m_activeInterlocutorName = name;
    emit activeInterlocutorNameChanged();
}
