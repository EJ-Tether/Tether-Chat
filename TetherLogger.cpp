// Begin source file TetherLogger.cpp
#include "TetherLogger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>

// ---------------------------------------------------------------------------
// Log format
//
//   [2026-03-19T20:48:55+01:00] [Elara] REQUEST [NormalReply]
//   {"model":"gpt-4o","input":[...]}
//   ──────────────────────────────────────────────────────────────────────────
//
// Each entry is self-contained: timestamp, interlocutor, direction, kind,
// and the full raw JSON payload on the following line(s).
// A separator line (─ ×78) follows every entry for easy visual scanning.
// ---------------------------------------------------------------------------

static QMutex s_mutex;
static const QString SEPARATOR =
    QString(u'\u2500').repeated(78); // ──────── (78 × U+2500 BOX DRAWINGS LIGHT HORIZONTAL)

// Returns the absolute path to Tether.log, creating the parent directory
// if it does not yet exist.
static QString logFilePath()
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/TetherChats";
    QDir().mkpath(dir);
    return dir + "/Tether.log";
}

void TetherLogger::log(const QString &interlocutorName, const QString &direction,
                       const QString &kind, const QByteArray &payload)
{
    QMutexLocker locker(&s_mutex);

    QFile file(logFilePath());
    if (!file.open(QIODevice::Append | QIODevice::Text))
    {
        // Silently ignore: we must not crash the app because of a log failure.
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    // Header line
    const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    out << "[" << timestamp << "] "
        << "[" << interlocutorName << "] " << direction << " "
        << "[" << kind << "]\n";

    // Payload — pretty-print if it looks like JSON, otherwise dump raw
    out << QString::fromUtf8(payload) << "\n";

    // Separator
    out << SEPARATOR << "\n\n";

    file.close();
}
// End source file TetherLogger.cpp
