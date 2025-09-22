#include <exxotools/QmlPropertyHelper.h>

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
  explicit Interlocutor(const QString &apiKey, const QString &systemPrompt,
                        QObject *parent = nullptr);

  Q_INVOKABLE void sendRequest(const QString &prompt);
  // QML_READONLY_VAR_PROPERTY(title, QString)

 signals:
  void responseReceived(const QJsonObject &response);
  void errorOccurred(const QString &error);

 private:
  QUrl m_url;
  QString m_apiKey;
  QString m_model;
  QNetworkAccessManager *m_manager;
  QJsonObject m_systemMsg;
  const int REQUEST_TIMEOUT_MS = 100000;
  QMap<QNetworkReply *, QTimer *> m_requestTimers;
};
