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

#include <QJsonDocument>
#include <QJsonObject>

void TetherLogger::logMessage(const QString &interlocutorName, const ChatMessage &message)
{
    QMutexLocker locker(&s_mutex);

    QFile file(logFilePath());
    if (!file.open(QIODevice::Append | QIODevice::Text))
    {
        return;
    }

    QJsonObject obj = message.toJsonObject();
    obj["interlocutor"] = interlocutorName;
    obj["type"] = "dialogue";

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << QJsonDocument(obj).toJson(QJsonDocument::Compact) << "\n";
    file.close();
}

void TetherLogger::logCuration(const QString &interlocutorName, const QString &ancientMemory)
{
    QMutexLocker locker(&s_mutex);

    QFile file(logFilePath());
    if (!file.open(QIODevice::Append | QIODevice::Text))
    {
        return;
    }

    QJsonObject obj;
    obj["interlocutor"] = interlocutorName;
    obj["type"] = "curation";
    obj["content"] = ancientMemory;
    obj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << QJsonDocument(obj).toJson(QJsonDocument::Compact) << "\n";
    file.close();
}
// End source file TetherLogger.cpp
