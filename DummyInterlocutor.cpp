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

void DummyInterlocutor::sendRequest(const QString &prompt)
{
    qDebug() << "DummyInterlocutor received prompt:" << prompt;
    m_responseTimer->setInterval(500);

    QObject::connect(m_responseTimer, &QTimer::timeout, this, [this, prompt]() {
        // --- LOGIQUE DE SIMULATION DE RÉPONSE ---
        QString reversedPrompt = prompt;
        std::reverse(reversedPrompt.begin(), reversedPrompt.end());
        QString completionText = "Réponse bidon: " + reversedPrompt;

        // --- DÉBUT DE LA SIMULATION DU DÉCOMPTE DE TOKENS ---
        // On utilise la règle simple : 1 token pour 4 caractères.
        // La division entière s'occupe de l'arrondi.
        int promptTokens = prompt.length() / 4;
        int completionTokens = completionText.length() / 4;

        // On crée l'objet "usage" que le ChatModel s'attend à recevoir.
        QJsonObject usageObject;
        usageObject["prompt_tokens"] = promptTokens;
        usageObject["completion_tokens"] = completionTokens;

        qDebug() << "Dummy simulation: prompt_tokens =" << promptTokens << ", completion_tokens =" << completionTokens;
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
        responseObj["object"] = "chat.completion";
        responseObj["created"] = QDateTime::currentSecsSinceEpoch();
        responseObj["model"] = "dummy-model";

        // On insère notre objet "usage" simulé dans la réponse finale.
        responseObj["usage"] = usageObject;

        // On émet la réponse pour que le ChatModel puisse la traiter.
        emit responseReceived(responseObj);
    }, Qt::SingleShotConnection);

    m_responseTimer->start();
}
// End Source File: DummyInterlocutor.cpp
