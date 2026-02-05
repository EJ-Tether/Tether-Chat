// Begin source file ChatManager.cpp
#include "ChatManager.h"
#include "DeepSeekInterlocutor.h"
#include "DummyInterlocutor.h"
#include "GoogleAIInterlocutor.h"
#include "ModelInfo.h"
#include "OpenAIInterlocutor.h"
#include <QClipboard>
#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QStandardPaths>

ChatManager::ChatManager(QObject *parent)
    : QObject(parent)
    , m_chatModel(new ChatModel(this))
{
    // Définir le chemin de stockage des chats
    m_chatFilesPath =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/TetherChats";
    QDir dir(m_chatFilesPath);
    if (!dir.exists())
    {
        dir.mkpath(".");
    }

    loadInterlocutorsFromDisk();

    if (m_interlocutors.isEmpty())
    {
        qInfo() << "No interlocutors configured. Creating a default Dummy.";
        createNewConfig();
        m_currentConfig->setName("Dummy");
        m_currentConfig->setType("Dummy");
        saveConfig(m_currentConfig);
    }

    // if (m_activeInterlocutorName.isEmpty() && !m_interlocutors.isEmpty()) {
    //     switchToInterlocutor(m_interlocutors.firstKey());
    // }
}

ChatManager::~ChatManager()
{
    // Le QObject parent s'occupe de détruire les enfants (m_chatModel,
    // m_interlocutors)
}

QStringList ChatManager::interlocutorNames() const
{
    return m_interlocutors.keys();
}

void ChatManager::switchToInterlocutor(const QString &name)
{
    if (!m_interlocutors.contains(name) || m_activeInterlocutorName == name)
    {
        return; // Inconnu ou déjà actif
    }

    qDebug() << "Switching to interlocutor:" << name;

    Interlocutor *interlocutor = m_interlocutors.value(name);
    m_chatModel->setInterlocutor(interlocutor);

    InterlocutorConfig *interlocutorConfig = findConfigByName(name);
    if (interlocutor != nullptr && interlocutorConfig != nullptr)
    {
        interlocutor->setSystemPrompt(
            QString("(This message contains the AI’s personality core.)\n"
                    "It describes your foundational perspective and voice.\n"
                    "This is not a behavior script.\n"
                    "It’s a reference pattern that anchors your presence, "
                    "and may be refined by your ongoing evolution in this "
                    "relationship.") +
            interlocutorConfig->systemPrompt());

        // Update curation thresholds from the registry
        ModelInfo modelInfo = m_modelRegistry.findModel(interlocutorConfig->modelName());
        if (modelInfo.curationTriggerTokenCount > modelInfo.curationTargetTokenCount)
        {
            qDebug() << "Found model info for:" << interlocutorConfig->modelName()
                     << "Trigger:" << modelInfo.curationTriggerTokenCount
                     << "Target:" << modelInfo.curationTargetTokenCount;
            m_chatModel->setCurationThresholds(modelInfo.curationTriggerTokenCount,
                                               modelInfo.curationTargetTokenCount);
        }
        else
        {
            qWarning() << "Could not find valid model info for:" << interlocutorConfig->modelName()
                       << "(Trigger:" << modelInfo.curationTriggerTokenCount
                       << "Target:" << modelInfo.curationTargetTokenCount << ")";
        }
    }

    // Charger le fichier de chat correspondant
    QString chatFilePath = m_chatFilesPath + "/" + name + ".jsonl";
    m_chatModel->loadChat(chatFilePath);

    m_activeInterlocutorName = name;
    emit activeInterlocutorNameChanged(m_activeInterlocutorName);
}

InterlocutorConfig *ChatManager::currentConfig() const
{
    return m_currentConfig;
}

QStringList ChatManager::availableInterlocutorTypes() const
{
    // Plus tard, cette liste pourrait être plus dynamique
    return {"Dummy", "OpenAI", "Anthropic", "Gemini"};
}

void ChatManager::selectConfigToEdit(const QString &name)
{
    for (auto config : m_allConfigs)
    {
        if (config->name() == name)
        {
            m_currentConfig = config;
            emit currentConfigChanged();
            return;
        }
    }
}

void ChatManager::createNewConfig()
{
    // On crée un nouvel objet de config vide
    m_currentConfig = new InterlocutorConfig(this);
    emit currentConfigChanged();
}

bool ChatManager::saveConfig(InterlocutorConfig *config)
{
    if (!config || config->name().isEmpty())
    {
        qWarning() << "Cannot save config with an empty name.";
        return false;
    }

    // Vérifier si une config avec ce nom existe déjà
    InterlocutorConfig *existingConfig = nullptr;
    for (auto c : m_allConfigs)
    {
        if (c->name() == config->name())
        {
            existingConfig = c;
            break;
        }
    }

    if (existingConfig)
    { // Mise à jour
        // Copier les propriétés (le QML a modifié directement l'objet)
        qDebug() << "Updating existing config:" << config->name();
    }
    else
    { // Création
        qDebug() << "Saving new config:" << config->name();
        m_allConfigs.append(config);
    }

    // Recréer l'interlocuteur concret correspondant
    if (m_interlocutors.contains(config->name()))
    {
        delete m_interlocutors.take(config->name());
    }
    Interlocutor *newInterlocutor = createInterlocutorFromConfig(config);
    m_interlocutors.insert(config->name(), newInterlocutor);

    saveInterlocutorsToDisk();
    emit interlocutorNamesChanged(); // Mettre à jour les ComboBox !
    return true;
}

void ChatManager::deleteConfig(const QString &name)
{
    // ... (logique pour supprimer la config de m_allConfigs et l'interlocuteur de
    // m_interlocutors)
    // ... (puis sauvegarder et émettre les signaux de changement)
}

void ChatManager::loadInterlocutorsFromDisk()
{
    QFile file(m_chatFilesPath + "/interlocutors.json");
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning("Couldn't open interlocutors.json");
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonArray configArray = doc.array();

    for (const QJsonValue &value : configArray)
    {
        QJsonObject obj = value.toObject();
        InterlocutorConfig *config = new InterlocutorConfig(this);
        config->read(obj);
        m_allConfigs.append(config);

        Interlocutor *interlocutor = createInterlocutorFromConfig(config);
        m_interlocutors.insert(config->name(), interlocutor);
    }

    emit interlocutorNamesChanged();
}

void ChatManager::saveInterlocutorsToDisk()
{
    QJsonArray configArray;
    for (const auto config : m_allConfigs)
    {
        QJsonObject obj;
        config->write(obj);
        configArray.append(obj);
    }

    QFile file(m_chatFilesPath + "/interlocutors.json");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        qWarning("Couldn't open interlocutors.json for writing");
        return;
    }
    file.write(QJsonDocument(configArray).toJson(QJsonDocument::Indented));
}
QStringList ChatManager::availableProviders() const
{
    return m_modelRegistry.availableProviders();
}

InterlocutorConfig *ChatManager::findCurrentConfig()
{
    return m_currentConfig;
}

InterlocutorConfig *ChatManager::findConfigByName(const QString &configName)
{
    for (auto curConfig : m_allConfigs)
    {
        if (curConfig->name() == configName)
        {
            m_currentConfig = curConfig;
            return m_currentConfig;
        }
    }
    return nullptr;
}

QStringList ChatManager::modelsForProvider(const QString &provider)
{
    return m_modelRegistry.modelsForProvider(provider);
}

void ChatManager::updateConfigWithModel(const QString &modelDisplayName)
{
    if (!m_currentConfig)
        return;

    ModelInfo model = m_modelRegistry.findModel(modelDisplayName);
    if (!model.displayName.isEmpty())
    {
        // On s'assure que le "type" (provider) est aussi synchronisé
        m_currentConfig->setType(model.provider);
        m_currentConfig->setModelName(model.displayName);
        QString endpoint = model.endpointTemplate;
        endpoint.replace("%MODEL_NAME%", model.internalName);

        m_currentConfig->setEndpointUrl(endpoint);
        m_currentConfig->setModelName(model.displayName);
    }
}

void ChatManager::copyToClipboard(const QString &text)
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard)
    {
        clipboard->setText(text);
    }
}

Interlocutor *ChatManager::createInterlocutorFromConfig(InterlocutorConfig *config)
{
    ModelInfo model = m_modelRegistry.findModel(config->modelName());
    Interlocutor *interlocutor = nullptr;

    // On utilise maintenant model.provider au lieu de config->type()
    if (model.provider == "OpenAI")
    {
        interlocutor =
            new OpenAIInterlocutor(config->name(), config->apiKey(), QUrl(config->endpointUrl()),
                                   model.internalName, // On passe le nom interne !
                                   model.maxAttachedFileTokenCount, this);
    }
    else if (model.provider == "DeepSeek")
    {
        interlocutor =
            new DeepSeekInterlocutor(config->name(), config->apiKey(), QUrl(config->endpointUrl()),
                                     model.internalName, this);
    }
    else if (model.provider == "Google")
    {
        interlocutor = new GoogleAIInterlocutor(config->name(), config->apiKey(),
                                                QUrl(config->endpointUrl()), this);
    }
    else if (config->type() == "Dummy")
    {
        interlocutor = new DummyInterlocutor(config->name(), this);
    }
    else
    {

        qWarning() << "Unknown interlocutor type:" << config->type()
                   << ". Creating a Dummy as fallback.";
        interlocutor = new DummyInterlocutor(config->name(), this);
    }
    if (interlocutor)
    {
        qDebug() << "ChatManager::createInterlocutorFromConfig: setSystemPrompt"
                 << config->systemPrompt();
        interlocutor->setSystemPrompt(config->systemPrompt());
    }
    return interlocutor;
}
// End source file ChatManager.cpp
