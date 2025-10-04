#include "OpenAIInterlocutor.h"

#include <QDebug>
#include <QUrl>

OpenAIInterlocutor::OpenAIInterlocutor(const QString &apiKey, // Secret API Key
                           QUrl url, // For instance, for OpenAI: "https://api.openai.com/v1/chat/completions"
                           QString model, // For instance "gpt-4o"
                           QObject *parent)
    : QObject(parent), m_apiKey(apiKey), m_url(url), m_model(model) {
  m_manager = new QNetworkAccessManager(this);
}

void OpenAIInterlocutor::setSystemPrompt(const QString &systemPrompt) {
    m_systemMsg["role"] = "system";
    m_systemMsg["content"] = systemPrompt;
}

void Interlocutor::sendRequest(const QString &prompt) {
  QNetworkRequest request(m_url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  request.setRawHeader("Authorization",
                       QString("Bearer %1").arg(m_apiKey).toUtf8());

  // Build JSON payload
  QJsonObject json;
  json["model"] = m_model;

  QJsonArray messages;
  messages.append(m_systemMsg);

  QJsonObject userMsg;
  userMsg["role"] = "user";
  userMsg["content"] = prompt;
  messages.append(userMsg);

  json["messages"] = messages;
  QJsonDocument doc(json);
  QByteArray data = doc.toJson();

  QNetworkReply *reply = m_manager->post(request, data);

  // Création du timer pour le timeout
  auto *timeoutTimer = new QTimer(this);
  timeoutTimer->setSingleShot(true);
  timeoutTimer->setInterval(REQUEST_TIMEOUT_MS);

  // Associer le timer à la requête
  m_requestTimers[reply] = timeoutTimer;

  // Ensure timer is cleaned up with the reply lifecycle
  QObject::connect(reply, &QObject::destroyed, this, [this, reply]() {
    auto it = m_requestTimers.find(reply);
    if (it != m_requestTimers.end()) {
      if (it.value()) {
        it.value()->stop();
        it.value()->deleteLater();
      }
      m_requestTimers.erase(it);
    }
  });

  // Quand le timer expire, on annule la requête et on émet un signal d’erreur
  connect(timeoutTimer, &QTimer::timeout, this, [this, reply]() {
    if (reply->isRunning()) {
      qDebug() << "Request timed out!";
      reply->abort();  // Annule la requête
      emit errorOccurred("Request timed out after " +
                         QString::number(REQUEST_TIMEOUT_MS / 1000) +
                         " seconds.");
    }
  });

  // Démarrer le timer
  timeoutTimer->start();

  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    // Stop and cleanup the timeout timer if present
    auto it = m_requestTimers.find(reply);
    if (it != m_requestTimers.end()) {
      if (it.value()) {
        it.value()->stop();
        it.value()->deleteLater();
      }
      m_requestTimers.erase(it);
    }
    if (reply->error() == QNetworkReply::NoError) {
      QByteArray responseData = reply->readAll();
      qDebug() << "Raw API Response: " << responseData;

      // Convert QByteArray to JSON
      QJsonDocument jsonResponse = QJsonDocument::fromJson(responseData);
      if (!jsonResponse.isNull() && jsonResponse.isObject()) {
        QJsonObject responseObj = jsonResponse.object();

        if (responseObj.contains("choices") &&
            responseObj["choices"].isArray()) {
          QJsonArray choicesArray = responseObj["choices"].toArray();
          if (!choicesArray.isEmpty() && choicesArray[0].isObject()) {
            QJsonObject firstChoice = choicesArray[0].toObject();
            if (firstChoice.contains("message") &&
                firstChoice["message"].isObject()) {
              QJsonObject message = firstChoice["message"].toObject();
              if (message.contains("content")) {
                QString content = message["content"].toString();

                message["content"] = content;
                firstChoice["message"] = message;
                choicesArray[0] = firstChoice;
                responseObj["choices"] = choicesArray;
              }
            }
          }
        }
        emit responseReceived(responseObj);  // Emit JSON
      } else {
        emit errorOccurred("Invalid JSON response from API endpoint.");
      }
    } else {
      emit errorOccurred(reply->errorString());
    }

    reply->deleteLater();
  });
}

// OpenAIInterlocutor::sendChatMessage()
// OpenAIInterlocutor::sendMemoryFileForCuration()
