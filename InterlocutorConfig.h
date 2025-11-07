// Begin source file InterlocutorConfig.h
#ifndef INTERLOCUTORCONFIG_H
#define INTERLOCUTORCONFIG_H

#include <QObject>
#include <QString>
#include <QJsonObject>

class InterlocutorConfig : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString type READ type WRITE setType NOTIFY typeChanged)
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY apiKeyChanged)
    Q_PROPERTY(QString endpointUrl READ endpointUrl WRITE setEndpointUrl NOTIFY endpointUrlChanged)
    Q_PROPERTY(QString systemPrompt READ systemPrompt WRITE setSystemPrompt NOTIFY systemPromptChanged)
    Q_PROPERTY(QString modelName READ modelName WRITE setModelName NOTIFY modelNameChanged)
    Q_PROPERTY(QString ancientMemoryFileId READ ancientMemoryFileId WRITE setAncientMemoryFileId
                   NOTIFY ancientMemoryFileIdChanged)

public:
    explicit InterlocutorConfig(QObject *parent = nullptr);

    QString name() const;
    void setName(const QString &name);

    QString type() const;
    void setType(const QString &type);

    QString apiKey() const;
    void setApiKey(const QString &apiKey);

    QString endpointUrl() const;
    void setEndpointUrl(const QString &endpointUrl);

    QString systemPrompt() const;
    void setSystemPrompt(const QString &systemPrompt);

    // Fonctions pour la persistance JSON
    void read(const QJsonObject &json);
    void write(QJsonObject &json) const;
    
    QString modelName() const;
    void setModelName(const QString &modelName);

    QString ancientMemoryFileId();
    void setAncientMemoryFileId(const QString &fileId);

signals:
    void nameChanged();
    void typeChanged();
    void apiKeyChanged();
    void endpointUrlChanged();
    void systemPromptChanged();
    void modelNameChanged();
    void ancientMemoryFileIdChanged();

private:
    QString m_name;
    QString m_type;
    QString m_apiKey;
    QString m_endpointUrl;
    QString m_systemPrompt;
    QString m_modelName;
    QString m_ancientMemoryFileId;
};

#endif // INTERLOCUTORCONFIG_H
// End source file InterlocutorConfig.h
