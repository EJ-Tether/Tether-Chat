// Begin source file AnthropicInterlocutor.h
#pragma once
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include "Interlocutor.h"


class AnthropicInterlocutor : public Interlocutor {
    Q_OBJECT
public:
    explicit AnthropicInterlocutor(
        QString interlocutorName,
        const QString &apiKey,   // Secret API Key
        const QUrl &url,         // "https://api.anthropic.com/v1/messages"
        const QString &model,    // e.g. "claude-sonnet-4-6"
        QObject *parent);

    void sendRequest(
        const QList<ChatMessage> &history,
        const QString& ancientMemory,
        const InterlocutorReply::Kind kind,
        const QStringList &attachmentFileIds) override;

    void uploadFile(QString fileName, const QByteArray &content, const QString &purpose) override;
    void deleteFile(const QString &fileId) override;

private:
    QString m_apiKey;
    QUrl m_url;
    QString m_model;
    QNetworkAccessManager *m_manager;
    const int REQUEST_TIMEOUT_MS = 360000;
    const int MAX_OUTPUT_TOKENS  = 8096;
};
// End source file AnthropicInterlocutor.h
