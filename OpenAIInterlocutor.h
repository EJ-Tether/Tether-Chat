#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include "Interlocutor.h"


class OpenAIInterlocutor : public Interlocutor {
  Q_OBJECT
 public:
  explicit OpenAIInterlocutor(  QString interlocutorName,
                                const QString &apiKey, // Secret API Key
                                QUrl url, // For instance, for OpenAI: "https://api.openai.com/v1/chat/completions"
                                QString model, // For instance "gpt-4o"
                                QObject *parent);

  Q_INVOKABLE void sendRequest(const QString &prompt);  
  void setSystemPrompt(const QString &systemPrompt);

  // Les signaux ci-dessous sont hérités de la classe mère `Interlocutor`
  // signals:
  //     void responseReceived(const QJsonObject &response);
  //     void errorOccurred(const QString &error);

 private:
  QUrl m_url;
  QString m_apiKey;
  QString m_model;
  QNetworkAccessManager *m_manager;
  QJsonObject m_systemMsg;
  const int REQUEST_TIMEOUT_MS = 100000;
  QMap<QNetworkReply *, QTimer *> m_requestTimers;
};
