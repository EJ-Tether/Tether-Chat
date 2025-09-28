#ifndef DUMMYINTERLOCUTOR_H
#define DUMMYINTERLOCUTOR_H

#include <QObject>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray> // Ajouté pour construire la réponse bidon

class DummyInterlocutor : public QObject
{
    Q_OBJECT
public:
    // Le constructeur doit correspondre à celui de Interlocutor pour être un substitut facile
    explicit DummyInterlocutor(QObject *parent = nullptr);

    Q_INVOKABLE void sendRequest(const QString &prompt);

signals:
    void responseReceived(const QJsonObject &response);
    void errorOccurred(const QString &error);

private:
    QTimer *m_responseTimer; // Pour simuler un délai de réponse
    // On peut les garder pour que l'interface soit la même, mais les ignorer
    QString m_apiKey;
    QUrl m_url;
    QString m_model;
    QString m_systemPrompt; // On garde le systemPrompt pour le constructeur
};

#endif // DUMMYINTERLOCUTOR_H
