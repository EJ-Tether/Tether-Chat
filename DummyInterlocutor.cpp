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
        QString reversedPrompt = prompt;
        std::reverse(reversedPrompt.begin(), reversedPrompt.end());

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
        responseObj["usage"] = QJsonObject();

        emit responseReceived(responseObj);
    }, Qt::SingleShotConnection);

    m_responseTimer->start();
}
