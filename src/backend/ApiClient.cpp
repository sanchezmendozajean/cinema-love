#include "ApiClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

ApiClient::ApiClient(QObject *parent) : QObject(parent) {}

void ApiClient::setBaseUrl(const QString &u)
{
    if (u == m_baseUrl) return;
    m_baseUrl = u;
    emit configChanged();
}

void ApiClient::setToken(const QString &t)
{
    if (t == m_token) return;
    m_token = t;
    emit configChanged();
}

void ApiClient::request(const QString &method, const QString &path, const QVariant &body, const QJSValue &callback)
{
    QJSValue cb = callback;
    if (!ready()) {
        if (cb.isCallable()) cb.call({QJSValue(0), QJSValue(QJSValue::NullValue), QJSValue(tr("El backend no está listo."))});
        return;
    }
    QNetworkRequest req(QUrl(m_baseUrl + path));
    req.setRawHeader("X-Api-Key", m_token.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QByteArray payload = body.isValid() && !body.isNull() ? QJsonDocument::fromVariant(body).toJson(QJsonDocument::Compact) : QByteArray();

    QNetworkReply *r = nullptr;
    const QString m = method.toUpper();
    if (m == QLatin1String("GET")) r = m_nam.get(req);
    else if (m == QLatin1String("POST")) r = m_nam.post(req, payload.isEmpty() ? QByteArray("{}") : payload);
    else if (m == QLatin1String("PUT")) r = m_nam.put(req, payload.isEmpty() ? QByteArray("{}") : payload);
    else r = m_nam.sendCustomRequest(req, m.toUtf8(), payload);

    connect(r, &QNetworkReply::finished, this, [this, r, cb]() mutable {
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = r->readAll();
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
        QVariant data = pe.error == QJsonParseError::NoError ? doc.toVariant() : QVariant();
        QString err;
        if (r->error() != QNetworkReply::NoError && status == 0) err = r->errorString();
        else if (status >= 400) err = data.toMap().value(QStringLiteral("error")).toString().isEmpty() ? QStringLiteral("Error HTTP %1").arg(status)
                                                                                                          : data.toMap().value(QStringLiteral("error")).toString();
        r->deleteLater();
        QJSEngine *eng = qjsEngine(this);
        if (cb.isCallable() && eng) cb.call({QJSValue(status), eng->toScriptValue(data), QJSValue(err)});
    });
}
