// Begin source file InterlocutorConfig.cpp
#include "InterlocutorConfig.h"

InterlocutorConfig::InterlocutorConfig(QObject *parent) : QObject(parent) {}

QString InterlocutorConfig::name() const { return m_name; }
void InterlocutorConfig::setName(const QString &name) {
    if (m_name != name) {
        m_name = name;
        emit nameChanged();
    }
}

QString InterlocutorConfig::type() const { return m_type; }
void InterlocutorConfig::setType(const QString &type) {
    if (m_type != type) {
        m_type = type;
        emit typeChanged();
    }
}

QString InterlocutorConfig::apiKey() const { return m_apiKey; }
void InterlocutorConfig::setApiKey(const QString &apiKey) {
    if (m_apiKey != apiKey) {
        m_apiKey = apiKey;
        emit apiKeyChanged();
    }
}

QString InterlocutorConfig::endpointUrl() const { return m_endpointUrl; }
void InterlocutorConfig::setEndpointUrl(const QString &endpointUrl) {
    if (m_endpointUrl != endpointUrl) {
        m_endpointUrl = endpointUrl;
        emit endpointUrlChanged();
    }
}

QString InterlocutorConfig::systemPrompt() const { return m_systemPrompt; }
void InterlocutorConfig::setSystemPrompt(const QString &systemPrompt) {
    if (m_systemPrompt != systemPrompt) {
        m_systemPrompt = systemPrompt;
        emit systemPromptChanged();
    }
}

QString InterlocutorConfig::modelName() const
{
    return m_modelName;
}
void InterlocutorConfig::setModelName(const QString &modelName)
{
    if (m_modelName != modelName) {
        m_modelName = modelName;
        emit modelNameChanged();
    }
}

QString InterlocutorConfig::ancientMemoryFileId() const
{
    return m_ancientMemoryFileId;
}
void InterlocutorConfig::setAncientMemoryFileId(const QString &ancientMemoryFileId)
{
    if (m_ancientMemoryFileId != ancientMemoryFileId) {
        m_ancientMemoryFileId = ancientMemoryFileId;
        emit ancientMemoryFileIdChanged();
    }
}

void InterlocutorConfig::read(const QJsonObject &json) {
    setName(json["name"].toString());
    setType(json["type"].toString());
    setApiKey(json["apiKey"].toString());
    setEndpointUrl(json["endpointUrl"].toString());
    setSystemPrompt(json["systemPrompt"].toString());
    setModelName(json["modelName"].toString());

}

void InterlocutorConfig::write(QJsonObject &json) const {
    json["name"] = m_name;
    json["type"] = m_type;
    json["apiKey"] = m_apiKey;
    json["endpointUrl"] = m_endpointUrl;
    json["systemPrompt"] = m_systemPrompt;
    json["modelName"] = m_modelName;
}
// End source file InterlocutorConfig.cpp
