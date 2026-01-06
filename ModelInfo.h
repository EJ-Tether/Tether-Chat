// Begin source file Modelinfo.h
#ifndef MODELINFO_H
#define MODELINFO_H

#include <QString>

struct ModelInfo {
    QString provider;         // "OpenAI", "Google", etc.
    QString displayName;      // "GPT-4o", "Gemini 1.5 Pro"
    QString internalName;     // "gpt-4o", "gemini-1.5-pro"
    QString endpointTemplate; // "https://.../%MODEL_NAME%..." (%MODEL_NAME% sera remplacé si besoin)
    int curationTriggerTokenCount; // Seuil de déclenchement de la curation
    int curationTargetTokenCount;  // Taille cible de la mémoire après curation
    int maxAttachedFileTokenCount; // Taille maximum des attachements autorisés
};

#endif // MODELINFO_H
// End source file Modelinfo.h
