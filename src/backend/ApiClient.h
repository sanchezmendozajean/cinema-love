#pragma once
#include <QJSValue>
#include <QNetworkAccessManager>
#include <QObject>
#include <QQmlEngine>
#include <QVariant>

/** Cliente mínimo de la API v1 del backend (JSON sobre HTTP con X-Api-Key). Las respuestas llegan por callback: cb(status, datos). */
class ApiClient : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString baseUrl READ baseUrl WRITE setBaseUrl NOTIFY configChanged)
    Q_PROPERTY(QString token READ token WRITE setToken NOTIFY configChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY configChanged)

public:
    explicit ApiClient(QObject *parent = nullptr);

    QString baseUrl() const { return m_baseUrl; }
    QString token() const { return m_token; }
    bool ready() const { return !m_baseUrl.isEmpty() && !m_token.isEmpty(); }
    void setBaseUrl(const QString &u);
    void setToken(const QString &t);

    /** method: GET | POST | PUT | DELETE. body: objeto JS (se envía como JSON) o null. callback(status, datos, mensajeDeError). */
    Q_INVOKABLE void request(const QString &method, const QString &path, const QVariant &body, const QJSValue &callback);

signals:
    void configChanged();

private:
    QNetworkAccessManager m_nam;
    QString m_baseUrl, m_token;
};
