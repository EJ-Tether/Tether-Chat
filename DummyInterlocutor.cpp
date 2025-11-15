#include "DummyInterlocutor.h"
#include <QDebug>
#include <algorithm> // Pour std::reverse

DummyInterlocutor::DummyInterlocutor(QString interlocutorName, QObject *parent)
    : Interlocutor(interlocutorName, parent)
{
    // On n'a plus besoin d'un timer membre, QTimer::singleShot est plus simple.
}

void DummyInterlocutor::sendRequest(const QList<ChatMessage> &history,
                                    const QString& ancientMemory,
                                    const InterlocutorReply::Kind kind,
                                    const QStringList &attachmentFileIds)
{
    qDebug() << "DummyInterlocutor::sendRequest called with kind:"
             << (kind == InterlocutorReply::Kind::NormalMessage ? "Normal Message" : "Curation Result");

    if (history.isEmpty()) {
        emit errorOccurred("DummyInterlocutor received an empty history.");
        return;
    }

    // On simule un délai de réponse réseau de 500 ms
    QTimer::singleShot(500, this, [this, history, ancientMemory, kind]() {

        // --- 1. Préparation de la réponse textuelle ---
        // On prend le texte du dernier message de l'historique qu'on nous a passé
        const QString& lastPrompt = history.last().text();
        QString reversedPrompt = lastPrompt;
        std::reverse(reversedPrompt.begin(), reversedPrompt.end());
        QString completionText = "Réponse bidon: " + reversedPrompt;

        // --- 2. Préparation de la structure de réponse propre (InterlocutorReply) ---
        InterlocutorReply cleanReply;
        cleanReply.text = completionText;
        cleanReply.kind = kind; // On propage le 'kind' qu'on a reçu !

        // --- 3. Simulation du décompte de tokens ---
        int historyTokens = 0;
        for (const auto& msg : history) {
            historyTokens += msg.text().length() / 4;
        }

        // On simule un coût fixe pour le system prompt et la mémoire ancienne
        int systemTokens = 30;
        int ancientMemoryTokens = ancientMemory.length() / 4;
        int completionTokens = completionText.length() / 4;

        cleanReply.inputTokens = systemTokens + ancientMemoryTokens + historyTokens;
        cleanReply.outputTokens = completionTokens;
        cleanReply.totalTokens = cleanReply.inputTokens + cleanReply.outputTokens;

        qDebug() << "Dummy usage: input=" << cleanReply.inputTokens
                 << "output=" << cleanReply.outputTokens
                 << "total=" << cleanReply.totalTokens;

        // --- 4. Émission du signal avec la réponse propre ---
        emit replyReady(cleanReply);
    });
}

void DummyInterlocutor::uploadFile(const QByteArray &content, const QString &purpose)
{
    qDebug() << "DummyInterlocutor: Simulating upload for purpose '" << purpose << "'...";

    // On simule un délai d'upload de 1.5 secondes
    QTimer::singleShot(1500, this, [this, purpose]() {
        // On génère un ID de fichier bidon unique pour le debug
        QString dummyFileId = "dummy_file_" + QString::number(QDateTime::currentMSecsSinceEpoch());
        qDebug() << "DummyInterlocutor: Upload finished. Emitting fileUploaded with ID:" << dummyFileId;

        emit fileUploaded(dummyFileId, purpose);
    });
}

void DummyInterlocutor::deleteFile(const QString &fileId)
{
    qDebug() << "DummyInterlocutor: Simulating deletion of file ID:" << fileId;

    // Simuler un petit délai de 500 ms
    QTimer::singleShot(500, this, [this, fileId]() {
        qDebug() << "DummyInterlocutor: Deletion finished for ID:" << fileId;
        emit fileDeleted(fileId, true);
    });
}
