#include "DummyInterlocutor.h"
#include <QDebug>
#include <algorithm> // Pour std::reverse

DummyInterlocutor::DummyInterlocutor(QObject *parent)
    : QObject(parent)
{
    m_apiKey = "dummy API";
    m_responseTimer = new QTimer(this);
    m_responseTimer->setSingleShot(true);
}


void DummyInterlocutor::sendRequest(const QString &prompt)
{
    qDebug() << "DummyInterlocutor received prompt:" << prompt;

    // Simuler un délai de réponse (par exemple, 500 ms)
    m_responseTimer->setInterval(500); // Vous pouvez ajuster ce délai

    // Connecter le timer pour émettre la réponse quand il expire
    QObject::connect(m_responseTimer, &QTimer::timeout, this, [this, prompt]() {
        // Inverser la chaîne de caractères
        QString reversedPrompt = prompt;
        std::reverse(reversedPrompt.begin(), reversedPrompt.end());

        // Construire une réponse QJsonObject qui imite la structure de l'API réelle
        QJsonObject message;
        message["role"] = "assistant";
        message["content"] = "Réponse bidon: " + reversedPrompt;

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
        responseObj["usage"] = QJsonObject(); // Un objet vide pour l'usage

        qDebug() << "DummyInterlocutor sending reversed response:" << reversedPrompt;
        emit responseReceived(responseObj);
    }, Qt::SingleShotConnection); // Utilisez Qt::SingleShotConnection pour déconnecter après une seule émission

    m_responseTimer->start();
}
