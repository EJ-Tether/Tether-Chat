// Begin source file ChatMessage.h
#ifndef CHATMESSAGE_H
#define CHATMESSAGE_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QJsonObject> // Pour la sérialisation/désérialisation JSON

// Structure pour représenter un seul message dans la conversation
struct ChatMessage
{
    Q_GADGET // Permet d'utiliser QVariant et d'autres fonctionnalités Qt avec cette structure

    Q_PROPERTY(bool isLocalMessage READ isLocalMessage WRITE setIsLocalMessage)
    Q_PROPERTY(QString text READ text WRITE setText)
    Q_PROPERTY(QDateTime timestamp READ timestamp WRITE setTimestamp)
    Q_PROPERTY(int promptTokens READ promptTokens WRITE setPromptTokens)
    Q_PROPERTY(int completionTokens READ completionTokens WRITE setCompletionTokens)
    Q_PROPERTY(QString role READ role WRITE setRole) // 'user' ou 'assistant'

public:
    // Constructeur par défaut
    ChatMessage() : m_isLocalMessage(false), m_promptTokens(0), m_completionTokens(0), m_role("") {}

    // Constructeur complet
    ChatMessage(bool local, const QString &txt, const QDateTime &ts, int pTokens, int cTokens, const QString &r)
        : m_isLocalMessage(local), m_text(txt), m_timestamp(ts), m_promptTokens(pTokens), m_completionTokens(cTokens), m_role(r) {}

    // Accesseurs
    bool isLocalMessage() const { return m_isLocalMessage; }
    QString text() const { return m_text; }
    QDateTime timestamp() const { return m_timestamp; }
    int promptTokens() const { return m_promptTokens; }
    int completionTokens() const { return m_completionTokens; }
    QString role() const { return m_role; }

    // Mutateurs
    void setIsLocalMessage(bool local) { m_isLocalMessage = local; }
    void setText(const QString &txt) { m_text = txt; }
    void setTimestamp(const QDateTime &ts) { m_timestamp = ts; }
    void setPromptTokens(int tokens) { m_promptTokens = tokens; }
    void setCompletionTokens(int tokens) { m_completionTokens = tokens; }
    void setRole(const QString &r) { m_role = r; }

    // Méthodes de sérialisation / désérialisation
    QJsonObject toJsonObject() const {
        QJsonObject obj;
        obj["isLocalMessage"] = m_isLocalMessage;
        obj["text"] = m_text;
        obj["timestamp"] = m_timestamp.toString(Qt::ISODate); // Format ISO pour la persistance
        obj["promptTokens"] = m_promptTokens;
        obj["completionTokens"] = m_completionTokens;
        obj["role"] = m_role;
        return obj;
    }

    static ChatMessage fromJsonObject(const QJsonObject &obj) {
        ChatMessage msg;
        msg.m_isLocalMessage = obj["isLocalMessage"].toBool();
        msg.m_text = obj["text"].toString();
        msg.m_timestamp = QDateTime::fromString(obj["timestamp"].toString(), Qt::ISODate);
        msg.m_promptTokens = obj["promptTokens"].toInt();
        msg.m_completionTokens = obj["completionTokens"].toInt();
        msg.m_role = obj["role"].toString();
        return msg;
    }

private:
    bool m_isLocalMessage;
    QString m_text;
    QDateTime m_timestamp;
    int m_promptTokens;
    int m_completionTokens;
    QString m_role; // "user" ou "assistant"
};

#endif // CHATMESSAGE_H
// End source file ChatMessage.h
