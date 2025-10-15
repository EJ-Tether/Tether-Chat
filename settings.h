// Begin source file settings.h
#ifndef SETTINGS_H
#define SETTINGS_H
#include <QSettings>
#include <QObject>
#include <QTranslator>

class Settings: public QObject
{
    Q_OBJECT
public:
    Settings();
    Settings(Settings const &) = delete;
    Settings(Settings const &&) = delete;
    ~Settings()  = default;
public slots:
    void applyNewLanguage(int language) { /* PLACEHOLDER : TO BE IMPLEMENTED */ };

signals:
    void retranslate() ;

private:
    QSettings m_settings;
    QTranslator m_translator;


};

#endif // SETTINGS_H
       // End source file settings.h
