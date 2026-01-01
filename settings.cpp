// Begin source file settings.cpp
#include "settings.h"

const QString KEY_LAST_INTERLOCUTOR = "app/lastUsedInterlocutor";

Settings::Settings() {}

QString Settings::lastUsedInterlocutor() const
{
    // Read the value from QSettings. If the key does not exist, return an empty string.
    return m_settings.value(KEY_LAST_INTERLOCUTOR, "").toString();
}

void Settings::setLastUsedInterlocutor(const QString &name)
{
    // We check whether the value has actually changed so as not to write to the disk unnecessarily.
    if (lastUsedInterlocutor() != name) {
        // Write the new value to QSettings.
        m_settings.setValue(KEY_LAST_INTERLOCUTOR, name);
        emit lastUsedInterlocutorChanged(); // we notify that the value has changed
    }
}
// End source file settings.cpp
