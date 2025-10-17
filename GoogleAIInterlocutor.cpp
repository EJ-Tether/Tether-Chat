// Begin GoogleAIInterlocutor.cpp
#include "GoogleAIInterlocutor.h"
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

GoogleAIInterlocutor::GoogleAIInterlocutor(QString interlocutorName,
                                           const QString &apiKey,
                                           const QUrl &url,
                                           QObject *parent)
    : Interlocutor(interlocutorName, parent)
    , m_apiKey(apiKey)
    , m_url(url)
{
    m_manager = new QNetworkAccessManager(this);
}

void GoogleAIInterlocutor::setSystemPrompt(const QString &systemPrompt)
{
    m_systemPrompt = systemPrompt;
}

void GoogleAIInterlocutor::sendRequest(const QList<ChatMessage> &history,
                                       const QStringList &attachmentFileIds)
{
    // L'URL de Gemini a besoin de la clé API en paramètre
    QUrl requestUrl(m_url);
    QUrlQuery query;
    query.addQueryItem("key", m_apiKey);
    requestUrl.setQuery(query);

    QNetworkRequest request(requestUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // --- Construction du payload JSON pour Google Gemini ---
    QJsonObject payload;
    QJsonArray contentsArray;

    QString firstUserMessage = history.first().text();
    // Gemini n'a pas de rôle "system". On préfixe le premier message utilisateur.
    if (!m_systemPrompt.isEmpty()) {
        firstUserMessage = m_systemPrompt + "\n\n--- DEBUT DE LA CONVERSATION ---\n\n"
                           + firstUserMessage;
    }

    // Gérer le premier message séparément
    QJsonObject firstTurn;
    firstTurn["role"] = "user";
    QJsonObject firstTextPart;
    firstTextPart["text"] = firstUserMessage;
    QJsonArray firstPartsArray;
    firstPartsArray.append(firstTextPart);
    firstTurn["parts"] = firstPartsArray;
    contentsArray.append(firstTurn);

    // Ajouter le reste de l'historique
    for (int i = 1; i < history.size(); ++i) {
        const ChatMessage &msg = history[i];
        QJsonObject turn;
        // La réponse de l'IA s'appelle "model" chez Google
        turn["role"] = msg.isLocalMessage() ? "user" : "model";
        QJsonObject textPart;
        textPart["text"] = msg.text();
        QJsonArray partsArray;
        partsArray.append(textPart);
        turn["parts"] = partsArray;
        contentsArray.append(turn);
    }

    payload["contents"] = contentsArray;

    QByteArray data = QJsonDocument(payload).toJson();
    QNetworkReply *reply = m_manager->post(request, data);

    connect(reply, &QNetworkReply::finished, this, [this, history, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument jsonResponse = QJsonDocument::fromJson(reply->readAll());
            if (jsonResponse.isNull() || !jsonResponse.isObject()) {
                emit errorOccurred("Invalid JSON response from Google API.");
                reply->deleteLater();
                return;
            }

            // --- Traduction de la réponse de Gemini au format OpenAI ---
            // Cela permet à ChatModel de ne connaître qu'un seul format de réponse !
            QJsonObject geminiResponse = jsonResponse.object();
            QJsonObject openAIStyleResponse;
            QJsonObject choice;
            QJsonObject message;
            QString content = "No content found.";

            if (geminiResponse.contains("candidates") && geminiResponse["candidates"].isArray()) {
                QJsonArray candidates = geminiResponse["candidates"].toArray();
                if (!candidates.isEmpty()) {
                    content = candidates[0]
                                  .toObject()["content"]
                                  .toObject()["parts"]
                                  .toArray()[0]
                                  .toObject()["text"]
                                  .toString();
                }
            }

            message["role"] = "assistant";
            message["content"] = content;
            choice["message"] = message;
            QJsonArray choices;
            choices.append(choice);
            openAIStyleResponse["choices"] = choices;

            // Simuler un objet "usage" car Gemini ne le fournit pas de la même manière
            int historyTokens = 0;
            for (const auto &msg : history) {
                historyTokens += msg.text().length() / 4;
            }
            int completionTokens = content.length() / 4;
            QJsonObject usage;
            usage["prompt_tokens"] = historyTokens;
            usage["completion_tokens"] = completionTokens;
            usage["total_tokens"] = historyTokens + completionTokens;
            openAIStyleResponse["usage"] = usage;

            emit responseReceived(openAIStyleResponse);

        } else {
            emit errorOccurred("API Error: " + reply->errorString());
        }
        reply->deleteLater();
    });
}
// End GoogleAIInterlocutor.cpp
