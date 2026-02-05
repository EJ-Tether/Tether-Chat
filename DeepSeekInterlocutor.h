#pragma once
#include "Interlocutor.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>


class DeepSeekInterlocutor : public Interlocutor
{
    Q_OBJECT
public:
    explicit DeepSeekInterlocutor(QString interlocutorName, const QString &apiKey, const QUrl &url,
                                  const QString &model, QObject *parent);

    void sendRequest(const QList<ChatMessage> &history, const QString &ancientMemory,
                     const InterlocutorReply::Kind kind,
                     const QStringList &attachmentFileIds) override;

    // File operations are not supported by DeepSeek chat API directly in this implementation
    void uploadFile(QString fileName, const QByteArray &content, const QString &purpose) override;
    void deleteFile(const QString &fileId) override;

private:
    QUrl m_url;
    QString m_apiKey;
    QString m_model;
    QNetworkAccessManager *m_manager;
    const int REQUEST_TIMEOUT_MS = 500000;
};
