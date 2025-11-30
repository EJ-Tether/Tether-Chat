#ifndef INTERLOCUTORREPLY_H
#define INTERLOCUTORREPLY_H

#include <QMetaType>
#include <QString>


struct InterlocutorReply {
    // On utilise un enum class pour un typage plus fort
    enum class Kind { NormalMessage, CurationResult };

    Kind kind = Kind::NormalMessage;

    QString text; // Texte principal de la réponse de l'IA
    int inputTokens = 0;
    int outputTokens = 0;
    int totalTokens = 0; // Ajoutons le total, c'est souvent fourni
    bool isIncomplete = false;
};

// Indispensable pour utiliser cette structure dans les signaux/slots
Q_DECLARE_METATYPE(InterlocutorReply);

#endif // INTERLOCUTORREPLY_H
