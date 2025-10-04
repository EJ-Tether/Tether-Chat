#ifndef INTERLOCUTOR_H
#define INTERLOCUTOR_H

#include <QObject>
#include <QJsonObject>

class Interlocutor : public QObject {
    Q_OBJECT
public:
    explicit Interlocutor(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~Interlocutor() {}

    Q_INVOKABLE virtual void sendRequest(const QString &prompt) = 0;
    virtual void setSystemPrompt(const QString &systemPrompt) {}

signals:
    void responseReceived(const QJsonObject &response);
    void errorOccurred(const QString &error);
};

#endif // INTERLOCUTOR_H
