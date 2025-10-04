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


class Interlocutor : public QObject {
  Q_OBJECT
 public:
  // From the name of the Interlocutor we'll find through the "Configuration" tab and configuration C++ model the
  // rest of the information: the API Key, the model name, the URL of the endpoint where to direct our requests,
  // etc... Only the DummyInterlocutor (debug class) needs no such information because there is no LLM involved
  explicit Interlocutor(QString interlocutorName, QObject *parent);

  Q_INVOKABLE virtual void sendRequest(const QString &prompt);
  virtual void setSystemPrompt(const QString &systemPrompt);

  signals:
  void responseReceived(const QJsonObject &response);
  void errorOccurred(const QString &error);

};
