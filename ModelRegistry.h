// Begin source file ModelRegistry
#ifndef MODELREGISTRY_H
#define MODELREGISTRY_H

#include <QObject>
#include <QList>
#include <QStringList>
#include "ModelInfo.h"

class ModelRegistry : public QObject
{
    Q_OBJECT
public:
    explicit ModelRegistry(QObject *parent = nullptr);

    // Renvoie la liste des fournisseurs uniques (pour la première ComboBox)
    QStringList availableProviders() const;

    // Renvoie la liste des noms d'affichage des modèles pour un fournisseur donné
    QStringList modelsForProvider(const QString& provider) const;

    // Trouve toutes les informations d'un modèle par son nom d'affichage
    ModelInfo findModel(const QString& displayName) const;

private:
    void populateModels(); // Remplit la base de données
    QList<ModelInfo> m_models;
};

#endif // MODELREGISTRY_H
// End source file ModelRegistry
