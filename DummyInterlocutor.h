// Begin Source File: DummyInterlocutor.h
#ifndef DUMMYINTERLOCUTOR_H
#define DUMMYINTERLOCUTOR_H

#include "Interlocutor.h"
#include <QTimer> // QTimer est un détail d'implémentation, on pourrait s'en passer ici

class DummyInterlocutor : public Interlocutor
{
    Q_OBJECT
public:
    explicit DummyInterlocutor(QString interlocutorName, QObject *parent = nullptr);

    // Implémentation de la nouvelle signature pour les requêtes de chat/curation
    void sendRequest(const QList<ChatMessage> &history,
                     const QString& ancientMemory,
                     const InterlocutorReply::Kind kind,
                     const QStringList &attachmentFileIds = {}) override;

    // Implémentation des méthodes de gestion de fichiers
    void uploadFile(const QByteArray &content, const QString &purpose) override;
    void deleteFile(const QString &fileId) override;

private:
    // On peut utiliser un seul QTimer pour toutes nos simulations de délai
    QTimer *m_delayTimer;
};

#endif // DUMMYINTERLOCUTOR_H
// End Source File: DummyInterlocutor.h
