// Begin Source File: DummyInterlocutor.h
#ifndef DUMMYINTERLOCUTOR_H
#define DUMMYINTERLOCUTOR_H

#include "Interlocutor.h"
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

class DummyInterlocutor : public Interlocutor
{
    Q_OBJECT
public:
    explicit DummyInterlocutor(QString interlocutorName, QObject *parent = nullptr);

    Q_INVOKABLE void sendRequest(const QList<ChatMessage> &history,
                                 const QStringList &attachmentFileIds) override;

    void uploadFile(const QByteArray &content, const QString &purpose) override {
        qDebug()<<"DummyInterlocutor::uploadFile:"<<content<<purpose;
    }
    void deleteFile(const QString &fileId) override {
        qDebug()<<"DummyInterlocutor::deleteFile:"<<fileId;
    }


private:
    QTimer *m_responseTimer;
};

#endif // DUMMYINTERLOCUTOR_H
// End Source File: DummyInterlocutor.h
