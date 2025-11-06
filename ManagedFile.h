#ifndef MANAGEDFILE_H
#define MANAGEDFILE_H

#include <QJsonObject>
#include <QObject>
#include <QString>

class ManagedFile : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString fileName READ fileName CONSTANT)
    Q_PROPERTY(QString fileId READ fileId WRITE setFileId NOTIFY fileIdChanged)
    Q_PROPERTY(Status status READ status WRITE setStatus NOTIFY statusChanged)

public:
    enum Status { Uploading, Ready, Error };
    Q_ENUM(Status)

    explicit ManagedFile(const QString &fileName, QObject *parent = nullptr);

    QString fileName() const;
    QString fileId() const;
    void setFileId(const QString &id);
    Status status() const;
    void setStatus(Status status);

    QJsonObject toJsonObject() const;
    static ManagedFile *fromJsonObject(const QJsonObject &obj, QObject *parent);
signals:
    void fileIdChanged();
    void statusChanged();

private:
    QString m_fileName;
    QString m_fileId;
    Status m_status;
};

#endif // MANAGEDFILE_H
