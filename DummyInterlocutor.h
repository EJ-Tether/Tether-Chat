// Begin Source File: DummyInterlocutor.h
#ifndef DUMMYINTERLOCUTOR_H
#define DUMMYINTERLOCUTOR_H

#include "Interlocutor.h"
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QRandomGenerator>

class DummyInterlocutor : public Interlocutor
{
    Q_OBJECT
public:
    explicit DummyInterlocutor(QString interlocutorName, QObject *parent = nullptr);

    Q_INVOKABLE void sendRequest(const QList<ChatMessage> &history,
                                 const QStringList &attachmentFileIds) override;

    void uploadFile(const QByteArray &content, const QString &purpose) override {
        qDebug()<<"DummyInterlocutor::uploadFile:"<<content<<purpose;
        QTimer::singleShot(900, this, [this, purpose]() {
            qDebug() << "DummyInterlocutor: Upload finished.";
            emit fileUploaded("dummy_id_" + QString::number(QRandomGenerator::global()->bounded(256)), purpose);
        });
    }
    void deleteFile(const QString &fileId) override {
        qDebug()<<"DummyInterlocutor::deleteFile:"<<fileId;
        QTimer::singleShot(900, this, [this, fileId]() {
            qDebug() << "DummyInterlocutor: Upload finished.";
            // On émet le signal APRÈS le délai
            emit fileDeleted(fileId, true);
        });
    }


private:
    QTimer *m_responseTimer;
};

#endif // DUMMYINTERLOCUTOR_H
// End Source File: DummyInterlocutor.h
