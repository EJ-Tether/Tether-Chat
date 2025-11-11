// Begin Source File: DummyInterlocutor.cpp
#include "DummyInterlocutor.h"
#include <QDebug>
#include <algorithm>

DummyInterlocutor::DummyInterlocutor(QString interlocutorName, QObject *parent)
    : Interlocutor(interlocutorName, parent)
{
    m_responseTimer = new QTimer(this);
    m_responseTimer->setSingleShot(true);
}

void DummyInterlocutor::sendRequest(const QList<ChatMessage> &history,
                                    const QStringList &attachmentFileIds)
{
    if (history.isEmpty())
        return;

    const QString &lastPrompt = history.last().text();
    qDebug() << "DummyInterlocutor received history with last prompt:" << lastPrompt;

    m_responseTimer->setInterval(500);

    QObject::connect(
        m_responseTimer,
        &QTimer::timeout,
        this,
        [this, history]() {
            const QString &lastPrompt = history.last().text();
            QString reversedPrompt = lastPrompt;
            std::reverse(reversedPrompt.begin(), reversedPrompt.end());
            QString completionText = "Réponse bidon: " + reversedPrompt;

            // --- NOUVELLE SIMULATION BASÉE SUR L'HISTORIQUE ---
            int systemTokens = 15; // Simule un prompt système constant
            int historyTokens = 0;
            for (const auto &msg : history) {
                historyTokens += msg.text().length() / 4;
            }
            int completionTokens = completionText.length() / 4;

            int totalPromptTokens = systemTokens + historyTokens;
            int totalTokens = totalPromptTokens + completionTokens;

            QJsonObject usageObject;
            usageObject["prompt_tokens"] = totalPromptTokens;
            usageObject["completion_tokens"] = completionTokens;
            usageObject["total_tokens"] = totalTokens;

            qDebug() << "Dummy usage based on history: total_tokens =" << totalTokens;
            // --- FIN DE LA SIMULATION DU DÉCOMPTE DE TOKENS ---

            // --- CONSTRUCTION DE LA RÉPONSE JSON COMPLÈTE ---
            QJsonObject message;
            message["role"] = "assistant";
            message["content"] = completionText;

            QJsonObject firstChoice;
            firstChoice["message"] = message;

            QJsonArray choicesArray;
            choicesArray.append(firstChoice);

            QJsonObject responseObj;
            responseObj["choices"] = choicesArray;
            responseObj["id"] = "dummy-chatcmpl-12345";
            responseObj["object"] = "v1/responses";
            responseObj["created"] = QDateTime::currentSecsSinceEpoch();
            responseObj["model"] = "dummy-model";

            // On insère notre objet "usage" simulé dans la réponse finale.
            responseObj["usage"] = usageObject;

            // On émet la réponse pour que le ChatModel puisse la traiter.
            emit responseReceived(responseObj);
        },
        Qt::SingleShotConnection);

    m_responseTimer->start();
}
// End Source File: DummyInterlocutor.cpp
