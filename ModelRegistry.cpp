// Begin source file ModelRegistry.cpp
#include "ModelRegistry.h"
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QDebug>

ModelRegistry::ModelRegistry(QObject *parent)
    : QObject(parent)
{
    populateModels();
}

void ModelRegistry::populateModels()
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/TetherChats";
    QDir dir(configDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    QString iniFilePath = configDir + "/models.ini";
    bool needToCreateDefaults = !QFile::exists(iniFilePath);

    QSettings settings(iniFilePath, QSettings::IniFormat);

    if (needToCreateDefaults) {
        qDebug() << "Creating default models.ini at" << iniFilePath;
        
        QList<ModelInfo> defaultModels = {
            {"OpenAI", "gpt-5.4", "gpt-5.4", "https://api.openai.com/v1/responses", 260000, 180000, 25000},
            {"OpenAI", "gpt-5.5", "gpt-5.5", "https://api.openai.com/v1/responses", 320000, 220000, 25000},
            {"OpenAI", "gpt-5.4-mini", "gpt-5.4-mini", "https://api.openai.com/v1/responses", 90000, 70000, 25000},
            {"DeepSeek", "DeepSeek-V3", "deepseek-chat", "https://api.deepseek.com/chat/completions", 320000, 200000, 0},
            {"DeepSeek", "DeepSeek-R1", "deepseek-reasoner", "https://api.deepseek.com/chat/completions", 320000, 200000, 0},
            {"DeepSeek", "deepseek-v4-pro", "deepseek-v4-pro", "https://api.deepseek.com/chat/completions", 320000, 200000, 0},
            {"Google", "Gemini 1.5 Pro", "gemini-1.5-pro", "https://generativelanguage.googleapis.com/v1beta/models/%MODEL_NAME%:generateContent", 120000, 100000, 25000},
            {"Google", "Gemini 1.0 Pro", "gemini-1.0-pro", "https://generativelanguage.googleapis.com/v1beta/models/%MODEL_NAME%:generateContent", 28000, 20000, 25000}
        };

        for (const auto& model : defaultModels) {
            settings.beginGroup(model.displayName);
            settings.setValue("provider", model.provider);
            settings.setValue("internalName", model.internalName);
            settings.setValue("endpointTemplate", model.endpointTemplate);
            settings.setValue("curationTriggerTokenCount", model.curationTriggerTokenCount);
            settings.setValue("curationTargetTokenCount", model.curationTargetTokenCount);
            settings.setValue("maxAttachedFileTokenCount", model.maxAttachedFileTokenCount);
            settings.endGroup();
        }
        settings.sync();
    }

    // Read all models from the INI file
    QStringList groups = settings.childGroups();
    for (const QString& groupName : groups) {
        settings.beginGroup(groupName);
        ModelInfo model;
        model.displayName = groupName;
        model.provider = settings.value("provider").toString();
        model.internalName = settings.value("internalName").toString();
        model.endpointTemplate = settings.value("endpointTemplate").toString();
        model.curationTriggerTokenCount = settings.value("curationTriggerTokenCount", 0).toInt();
        model.curationTargetTokenCount = settings.value("curationTargetTokenCount", 0).toInt();
        model.maxAttachedFileTokenCount = settings.value("maxAttachedFileTokenCount", 0).toInt();
        settings.endGroup();
        
        m_models.append(model);
    }
}

QStringList ModelRegistry::availableProviders() const
{
    QStringList providers;
    for (const auto &model : m_models)
    {
        if (!providers.contains(model.provider))
        {
            providers.append(model.provider);
        }
    }
    return providers;
}

QStringList ModelRegistry::modelsForProvider(const QString &provider) const
{
    QStringList names;
    for (const auto &model : m_models)
    {
        if (model.provider == provider)
        {
            names.append(model.displayName);
        }
    }
    return names;
}

ModelInfo ModelRegistry::findModel(const QString &displayName) const
{
    for (const auto &model : m_models)
    {
        if (model.displayName == displayName || model.internalName == displayName)
        {
            return model;
        }
    }
    return {}; // Retourne une structure vide si non trouvé
}
// End source file ModelRegistry.cpp
