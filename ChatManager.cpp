// Begin source file ChatManager.cpp
#include "ChatManager.h"
#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include "DummyInterlocutor.h"
#include "GoogleAIInterlocutor.h"
#include "ModelInfo.h"
#include "OpenAIInterlocutor.h"

ChatManager::ChatManager(QObject *parent)
    : QObject(parent)
    , m_chatModel(new ChatModel(this))
{
    // Définir le chemin de stockage des chats
    m_chatFilesPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                      + "/TetherChats";
    QDir dir(m_chatFilesPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    loadInterlocutorsFromDisk();

    if (m_interlocutors.isEmpty()) {
        qInfo() << "No interlocutors configured. Creating a default Dummy.";
        createNewConfig();
        m_currentConfig->setName("Dummy");
        m_currentConfig->setType("Dummy");
        saveConfig(m_currentConfig);
    }

    // Activer le premier interlocuteur de la liste
    // TODO : Plus tard: activer le dernier interlocuteur courant (qui sera sauvegardé dans le .settings)
    if (!m_interlocutors.isEmpty()) {
        switchToInterlocutor(m_interlocutors.firstKey());
    }
}

ChatManager::~ChatManager()
{
    // Le QObject parent s'occupe de détruire les enfants (m_chatModel, m_interlocutors)
}

void ChatManager::loadInterlocutors()
{
    // Pour l'instant, on charge juste le Dummy en dur.
    // Plus tard, on chargera tous les Interlocutor connus depuis l'objet Settings (qui reflète l'onglet Configuration)
    auto dummy = new DummyInterlocutor("Dummy", this);
    m_interlocutors.insert("Dummy", dummy);

    // Exemple de comment on ajouterai un vrai interlocuteur
    // auto openai = new OpenAIInterlocutor("ChatGPT-4o", this);
    // openai->setApiKey("MA_CLÉ_API");
    // m_interlocutors.insert("ChatGPT-4o", openai);

    emit interlocutorNamesChanged();
}

QStringList ChatManager::interlocutorNames() const
{
    return m_interlocutors.keys();
}

void ChatManager::switchToInterlocutor(const QString &name)
{
    if (!m_interlocutors.contains(name) || m_activeInterlocutorName == name) {
        return; // Inconnu ou déjà actif
    }

    qDebug() << "Switching to interlocutor:" << name;

    Interlocutor *interlocutor = m_interlocutors.value(name);
    m_chatModel->setInterlocutor(interlocutor);

    // Charger le fichier de chat correspondant
    QString chatFilePath = m_chatFilesPath + "/" + name + ".jsonl";
    m_chatModel->loadChat(chatFilePath);

    m_activeInterlocutorName = name;
    emit activeInterlocutorNameChanged();
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
    for (auto config : m_allConfigs) {
        if (config->name() == name) {
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
    if (!config || config->name().isEmpty()) {
        qWarning() << "Cannot save config with an empty name.";
        return false;
    }

    // Vérifier si une config avec ce nom existe déjà
    InterlocutorConfig *existingConfig = nullptr;
    for (auto c : m_allConfigs) {
        if (c->name() == config->name()) {
            existingConfig = c;
            break;
        }
    }

    if (existingConfig) { // Mise à jour
        // Copier les propriétés (le QML a modifié directement l'objet)
        qDebug() << "Updating existing config:" << config->name();
    } else { // Création
        qDebug() << "Saving new config:" << config->name();
        m_allConfigs.append(config);
    }

    // Recréer l'interlocuteur concret correspondant
    if (m_interlocutors.contains(config->name())) {
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
    // ... (logique pour supprimer la config de m_allConfigs et l'interlocuteur de m_interlocutors)
    // ... (puis sauvegarder et émettre les signaux de changement)
}

void ChatManager::loadInterlocutorsFromDisk()
{
    QFile file(m_chatFilesPath + "/interlocutors.json");
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("Couldn't open interlocutors.json");
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonArray configArray = doc.array();

    for (const QJsonValue &value : configArray) {
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
    for (const auto config : m_allConfigs) {
        QJsonObject obj;
        config->write(obj);
        configArray.append(obj);
    }

    QFile file(m_chatFilesPath + "/interlocutors.json");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("Couldn't open interlocutors.json for writing");
        return;
    }
    file.write(QJsonDocument(configArray).toJson(QJsonDocument::Indented));
}
QStringList ChatManager::availableProviders() const
{
    return m_modelRegistry.availableProviders();
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
    if (!model.displayName.isEmpty()) {
        m_currentConfig->setModelName(model.displayName);

        // Mettre à jour l'URL en remplaçant le placeholder
        QString endpoint = model.endpointTemplate;
        endpoint.replace("%MODEL_NAME%", model.internalName);
        m_currentConfig->setEndpointUrl(endpoint);
    }
}

Interlocutor *ChatManager::createInterlocutorFromConfig(InterlocutorConfig *config)
{
    ModelInfo model = m_modelRegistry.findModel(config->modelName());

    // On utilise maintenant model.provider au lieu de config->type()
    if (model.provider == "OpenAI") {
        return new OpenAIInterlocutor(config->name(),
                                      config->apiKey(),
                                      QUrl(config->endpointUrl()),
                                      model.internalName, // On passe le nom interne !
                                      this);
    }
    if (model.provider == "Google") {
        return new GoogleAIInterlocutor(config->name(),
                                        config->apiKey(),
                                        QUrl(config->endpointUrl()),
                                        this);
    }
    if (config->type() == "Dummy") {
        return new DummyInterlocutor(config->name(), this);
    }

    qWarning() << "Unknown interlocutor type:" << config->type()
               << ". Creating a Dummy as fallback.";
    return new DummyInterlocutor(config->name(), this);
}
// End source file ChatManager.cpp
