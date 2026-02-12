// Begin source file ModelRegistry.cpp
#include "ModelRegistry.h"

ModelRegistry::ModelRegistry(QObject *parent)
    : QObject(parent)
{
    populateModels();
}

void ModelRegistry::populateModels()
{
    // This is where we define all the models we support.
    // We could load this from a JSON file later for even more
    // flexibility.
    m_models.append({"OpenAI", "GPT-4o", "chatgpt-4o-latest", "https://api.openai.com/v1/responses",
                     100000, 80000, 25000});
    m_models.append({"OpenAI", "GPT-4 Turbo", "gpt-4-turbo", "https://api.openai.com/v1/responses",
                     22000, 12000, 25000});
    m_models.append({"OpenAI", "GPT-3.5 Turbo", "gpt-3.5-turbo",
                     "https://api.openai.com/v1/responses", 12000, 8000, 25000}); // Lower for 3.5
    m_models.append(
        {"OpenAI", "GPT-5", "gpt-5", "https://api.openai.com/v1/responses", 150000, 120000, 25000});
    m_models.append({"OpenAI", "GPT-5.1 mini", "gpt-5.1-mini",
                     "https://api.openai.com/v1/responses", 22000, 12000, 25000});
    m_models.append({"OpenAI", "GPT-5.1 nano", "gpt-5.1-nano",
                     "https://api.openai.com/v1/responses", 22000, 12000, 25000});
    m_models.append({"OpenAI", "GPT-4.1 (long context)", "gpt-4.1",
                     "https://api.openai.com/v1/responses", 90000, 70000, 25000});
    m_models.append({"DeepSeek", "DeepSeek-V3", "deepseek-chat",
                     "https://api.deepseek.com/chat/completions", 60000, 50000, 0});
    m_models.append({"DeepSeek", "DeepSeek-R1", "deepseek-reasoner",
                     "https://api.deepseek.com/chat/completions", 75000, 55000, 0});
    m_models.append({"Google", "Gemini 1.5 Pro", "gemini-1.5-pro",
                     "https://generativelanguage.googleapis.com/v1beta/models/"
                     "%MODEL_NAME%:generateContent",
                     120000, 100000, 25000});
    m_models.append({"Google", "Gemini 1.0 Pro", "gemini-1.0-pro",
                     "https://generativelanguage.googleapis.com/v1beta/models/"
                     "%MODEL_NAME%:generateContent",
                     28000, 20000, 25000});

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
