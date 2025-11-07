#include "ManagedFile.h"
#include "QDebug"

ManagedFile::ManagedFile(const QString &fileName, QObject *parent)
    : QObject(parent)
    , m_fileName(fileName)
    , m_status(Uploading)
{
    qDebug() << "ManagedFile::ManagedFile" << fileName;
}

QString ManagedFile::fileName() const
{
    return m_fileName;
}
QString ManagedFile::fileId() const { return m_fileId; }
void ManagedFile::setFileId(const QString &id) {
    if (m_fileId != id) {
        m_fileId = id;
        emit fileIdChanged();
    }
}
ManagedFile::Status ManagedFile::status() const { return m_status; }
void ManagedFile::setStatus(ManagedFile::Status status) {
    if (m_status != status) {
        m_status = status;
        emit statusChanged();
    }
}
QJsonObject ManagedFile::toJsonObject() const
{
    QJsonObject obj;
    obj["fileName"] = m_fileName;
    obj["fileId"] = m_fileId;
    return obj;
}

ManagedFile *ManagedFile::fromJsonObject(const QJsonObject &obj, QObject *parent)
{
    ManagedFile *file = new ManagedFile(obj["fileName"].toString(), parent);
    file->setFileId(obj["fileId"].toString());
    file->setStatus(ManagedFile::Ready); // Si c'est dans le fichier, c'est qu'il est prêt
    return file;
}
