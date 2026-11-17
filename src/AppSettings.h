#pragma once
#include <QObject>
#include <QSettings>
#include <QStandardPaths>
#include <QVariant>

/** Preferencias de la aplicación en el archivo de ajustes local del equipo (window.ini, grupo «app»): modo y servidor recordados. */
class AppSettings : public QObject
{
    Q_OBJECT
public:
    explicit AppSettings(QObject *parent = nullptr) : QObject(parent) {}
    static QString file() { return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/window.ini"); }
    static QString clientDir() { return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/cliente"); }
    Q_INVOKABLE QVariant value(const QString &key, const QVariant &fallback = {}) const
    {
        QSettings st(file(), QSettings::IniFormat);
        return st.value(QStringLiteral("app/") + key, fallback);
    }
    Q_INVOKABLE void setValue(const QString &key, const QVariant &v)
    {
        QSettings st(file(), QSettings::IniFormat);
        st.setValue(QStringLiteral("app/") + key, v);
    }
    Q_INVOKABLE void remove(const QString &key)
    {
        QSettings st(file(), QSettings::IniFormat);
        st.remove(QStringLiteral("app/") + key);
    }
};
