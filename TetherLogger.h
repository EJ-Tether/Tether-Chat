// Begin source file TetherLogger.h
#ifndef TETHERLOGGER_H
#define TETHERLOGGER_H

#include <QByteArray>
#include <QString>


// TetherLogger — Singleton stateless helper that appends every API
// request and response to ~/Documents/TetherChats/Tether.log.
//
// Format (one block per call):
//
//   [2026-03-19T20:48:55+01:00] [Elara] REQUEST [NormalReply]
//   {"model":"gpt-4o", ...}
//   ─────────────────────────────────────────────────────────────
//
// Thread-safe: a static QMutex protects the file writes.

class TetherLogger
{
public:
    // direction : "REQUEST" or "RESPONSE"
    // kind      : human-readable label, e.g. "NormalReply", "CurationResult",
    //             "AttachmentTokenCheck", "FileUpload"
    // payload   : raw JSON bytes sent to / received from the API
    static void log(const QString &interlocutorName, const QString &direction, const QString &kind,
                    const QByteArray &payload);
};

#endif // TETHERLOGGER_H
// End source file TetherLogger.h
