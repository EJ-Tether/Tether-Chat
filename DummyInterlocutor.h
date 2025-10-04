#ifndef DUMMYINTERLOCUTOR_H
#define DUMMYINTERLOCUTOR_H

#include "Interlocutor.h"
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>

class DummyInterlocutor : public Interlocutor
{
    Q_OBJECT
public:
    explicit DummyInterlocutor(QObject *parent = nullptr);

    Q_INVOKABLE void sendRequest(const QString &prompt) override;

private:
    QTimer *m_responseTimer;
};

#endif // DUMMYINTERLOCUTOR_H
