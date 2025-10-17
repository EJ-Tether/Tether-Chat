#ifndef MODELINFO_H
#define MODELINFO_H

#include <QString>

struct ModelInfo {
    QString provider;         // "OpenAI", "Google", etc.
    QString displayName;      // "GPT-4o", "Gemini 1.5 Pro"
    QString internalName;     // "gpt-4o", "gemini-1.5-pro"
    QString endpointTemplate; // "https://.../%MODEL_NAME%..." (%MODEL_NAME% sera remplacé si besoin)
};

#endif // MODELINFO_H
