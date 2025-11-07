// Begin source file settings.h
#ifndef SETTINGS_H
#define SETTINGS_H
#include <QSettings>
#include <QObject>
#include <QTranslator>

class Settings: public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString lastUsedInterlocutor READ lastUsedInterlocutor WRITE setLastUsedInterlocutor NOTIFY lastUsedInterlocutorChanged)
public:
    Settings();
    Settings(Settings const &) = delete;
    Settings(Settings const &&) = delete;
    ~Settings()  = default;


    QString lastUsedInterlocutor() const;
    void setLastUsedInterlocutor(const QString &name);

public slots:
    void applyNewLanguage(int language) { /* PLACEHOLDER : TO BE IMPLEMENTED */ };

signals:
    void retranslate() ;
    void lastUsedInterlocutorChanged();

private:
    QSettings m_settings;
    QTranslator m_translator;

};

#endif // SETTINGS_H
       // End source file settings.h
