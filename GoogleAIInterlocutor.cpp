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

void GoogleAIInterlocutor::sendRequest(
    const QList<ChatMessage> &history,
    const QString& ancientMemory,
    InterlocutorReply::Kind kind,
    const QStringList &attachmentFileIds)
{
    // L'URL de l'API v1beta de Gemini nécessite la clé en paramètre
    QUrl requestUrl(m_url);
    QUrlQuery query;
    query.addQueryItem("key", m_apiKey);
    requestUrl.setQuery(query);

    QNetworkRequest request(requestUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // --- Construction du payload JSON pour Google Gemini ---
    QJsonObject payload;
    QJsonArray contentsArray;

    // Pour Gemini, le "system prompt" et la "mémoire ancienne" sont fusionnés et
    // préfixés au premier tour de l'historique pour donner le contexte.
    QString fullContextPrefix;
    if (!m_systemPrompt.isEmpty()) {
        fullContextPrefix += m_systemPrompt + "\n\n";
    }
    if (!ancientMemory.isEmpty()) {
        fullContextPrefix += "--- LONG-TERM MEMORY SUMMARY ---\n" + ancientMemory + "\n\n";
    }

    for (int i = 0; i < history.size(); ++i) {
        const ChatMessage &msg = history[i];
        QJsonObject turn;

        // Le rôle de l'IA est "model" chez Google
        turn["role"] = msg.isLocalMessage() ? "user" : "model";

        QJsonObject textPart;
        QString messageText = msg.text();

        // Si c'est le tout premier message de l'historique, on ajoute notre préfixe
        if (i == 0 && msg.isLocalMessage() && !fullContextPrefix.isEmpty()) {
            messageText = fullContextPrefix + "--- CURRENT CONVERSATION ---\n" + messageText;
        }

        textPart["text"] = messageText;
        QJsonArray partsArray;
        partsArray.append(textPart);
        turn["parts"] = partsArray;

        contentsArray.append(turn);
    }

    payload["contents"] = contentsArray;

    QByteArray data = QJsonDocument(payload).toJson();
    QNetworkReply *reply = m_manager->post(request, data);

    connect(reply, &QNetworkReply::finished, this, [this, reply, kind]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument jsonResponse = QJsonDocument::fromJson(reply->readAll());
            if (jsonResponse.isNull() || !jsonResponse.isObject()) {
                emit errorOccurred("Invalid JSON response from Google API.");
                reply->deleteLater();
                return;
            }

            QJsonObject geminiResponse = jsonResponse.object();

            // --- PARSING de la réponse Gemini et création de InterlocutorReply ---
            InterlocutorReply cleanReply;
            cleanReply.kind = kind; // On propage le 'kind'

            // Extraire le texte de la réponse
            if (geminiResponse.contains("candidates") && geminiResponse["candidates"].isArray()) {
                QJsonArray candidates = geminiResponse["candidates"].toArray();
                if (!candidates.isEmpty()) {
                    // On navigue dans la structure spécifique à Gemini
                    cleanReply.text = candidates[0]
                                          .toObject()["content"]
                                          .toObject()["parts"]
                                          .toArray()[0]
                                          .toObject()["text"]
                                          .toString();
                }
            }

            // Extraire (ou simuler) l'usage des tokens
            // L'API Gemini v1beta ne renvoie pas toujours un décompte aussi clair que OpenAI.
            // S'il est absent, on fait une estimation.
            if (geminiResponse.contains("usageMetadata")) {
                QJsonObject usage = geminiResponse["usageMetadata"].toObject();
                cleanReply.inputTokens = usage["promptTokenCount"].toInt();
                cleanReply.outputTokens = usage["candidatesTokenCount"].toInt();
                cleanReply.totalTokens = usage["totalTokenCount"].toInt();
            } else {
                // Fallback : estimation si les métadonnées sont absentes
                qWarning() << "Google API response missing 'usageMetadata'. Estimating token counts.";
                // On peut faire une estimation rapide ici si besoin
            }

            emit replyReady(cleanReply);

        } else {
            emit errorOccurred("Google API Error: " + reply->errorString() + " | Body: " + reply->readAll());
        }
        reply->deleteLater();
    });
}
