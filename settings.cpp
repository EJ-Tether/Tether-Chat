// Begin source file settings.cpp
#include "settings.h"

const QString KEY_LAST_INTERLOCUTOR = "app/lastUsedInterlocutor";

Settings::Settings() {}

QString Settings::lastUsedInterlocutor() const
{
    // On lit la valeur depuis QSettings. Si la clé n'existe pas, on renvoie une chaîne vide.
    return m_settings.value(KEY_LAST_INTERLOCUTOR, "").toString();
}

void Settings::setLastUsedInterlocutor(const QString &name)
{
    // On vérifie si la valeur a réellement changé pour ne pas écrire sur le disque inutilement.
    if (lastUsedInterlocutor() != name) {
        // On écrit la nouvelle valeur dans QSettings.
        // QSettings s'occupe de la sauvegarder sur le disque de manière atomique.
        m_settings.setValue(KEY_LAST_INTERLOCUTOR, name);
        emit lastUsedInterlocutorChanged(); // On notifie que la valeur a changé.
    }
}
// End source file settings.cpp
