#include "Remote.h"

#include <QCryptographicHash>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QUrl>

namespace Remote {

namespace {

QNetworkAccessManager &nam()
{
    static thread_local QNetworkAccessManager *n = nullptr;
    if (!n) n = new QNetworkAccessManager();
    return *n;
}

QString fingerprintOf(const QSslCertificate &c)
{
    const QString hex = QString::fromLatin1(c.digest(QCryptographicHash::Sha256).toHex().toUpper());
    QStringList parts;
    for (int i = 0; i + 1 < hex.size(); i += 2) parts << hex.mid(i, 2);
    return parts.join(QLatin1Char(':'));
}

bool isHealth(const QJsonObject &o) { return o.value(QStringLiteral("status")).toString() == QLatin1String("ok") && o.value(QStringLiteral("app")).toString() == QLatin1String("cinemalove"); }

}

QString urlOf(const Dest &d)
{
    return QStringLiteral("%1://%2:%3").arg(d.https ? QStringLiteral("https") : QStringLiteral("http"), d.host.contains(QLatin1Char(':')) ? QStringLiteral("[%1]").arg(d.host) : d.host).arg(d.port);
}

std::optional<Dest> parseUrl(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty()) return std::nullopt;
    static const QRegularExpression scheme(QStringLiteral(R"(^[a-z][a-z0-9+.-]*://)"), QRegularExpression::CaseInsensitiveOption);
    const QUrl u(scheme.match(t).hasMatch() ? t : QStringLiteral("http://") + t);
    if (!u.isValid() || (u.scheme() != QLatin1String("http") && u.scheme() != QLatin1String("https")) || u.host().isEmpty()) return std::nullopt;
    Dest d;
    d.https = u.scheme() == QLatin1String("https");
    d.host = u.host();
    d.port = u.port(d.https ? 443 : 80);
    return d;
}

Response request(const Dest &d, const QString &method, const QString &pathAndQuery, const Headers &headers, const QByteArray &body, int timeoutMs)
{
    QUrl url(urlOf(d) + pathAndQuery);
    QNetworkRequest req(url);
    req.setTransferTimeout(timeoutMs);
    if (d.https) {
        // Sin reutilizar conexiones TLS: una conexión ya aceptada por un certificado confiado no debe servir a una petición que no lo está.
        req.setRawHeader("Connection", "close");
        req.setAttribute(QNetworkRequest::ConnectionCacheExpiryTimeoutSecondsAttribute, 0);
        // Tampoco se reanudan sesiones TLS: reanudar se salta la validación del certificado.
        QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
        tls.setSslOption(QSsl::SslOptionDisableSessionSharing, true);
        tls.setSslOption(QSsl::SslOptionDisableSessionPersistence, true);
        tls.setSslOption(QSsl::SslOptionDisableSessionTickets, true);
        req.setSslConfiguration(tls);
    }
    for (const auto &h : headers) req.setRawHeader(h.first, h.second);
    // Certificado confiado: se exige IGUAL a ese (huella), omitiendo solo el nombre de host (un autofirmado se emite para una IP que puede cambiar).
    QSslCertificate pinned;
    if (d.https && !d.certPem.isEmpty()) pinned = QSslCertificate(d.certPem.toLatin1(), QSsl::Pem);

    const QByteArray m = method.toUpper().toLatin1();
    QNetworkReply *reply = nullptr;
    if (m == "GET") reply = nam().get(req);
    else if (m == "POST") reply = nam().post(req, body);
    else if (m == "PUT") reply = nam().put(req, body);
    else reply = nam().sendCustomRequest(req, m, body);

    bool certErr = false;
    if (d.https) {
        QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply, pinned, &certErr](const QList<QSslError> &) {
            const QSslCertificate peer = reply->sslConfiguration().peerCertificate();
            if (!pinned.isNull() && !peer.isNull() && peer.digest(QCryptographicHash::Sha256) == pinned.digest(QCryptographicHash::Sha256)) reply->ignoreSslErrors();
            else certErr = true;
        });
    }
    if (d.https) {
        // Comprobación explícita al terminar el handshake: no depende de que el sistema avise (una sesión TLS reanudada no avisa).
        QObject::connect(reply, &QNetworkReply::encrypted, reply, [reply, pinned, &certErr] {
            const QSslConfiguration cfg = reply->sslConfiguration();
            const bool good = !pinned.isNull() ? cfg.peerCertificate().digest(QCryptographicHash::Sha256) == pinned.digest(QCryptographicHash::Sha256)
                                               : !cfg.peerCertificate().isSelfSigned();   // Schannel no permite verificar a mano: un autofirmado solo vale si está fijado
            if (!good) { certErr = true; reply->abort(); }
        });
    }
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    Response r;
    r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    r.body = reply->isOpen() ? reply->readAll() : QByteArray();
    r.certError = certErr || reply->error() == QNetworkReply::SslHandshakeFailedError;
    if (r.status == 0) r.error = reply->errorString();
    reply->deleteLater();
    return r;
}

JsonResponse requestJson(const Dest &d, const QString &pathAndQuery, const Headers &headers, const QJsonValue *body, int timeoutMs)
{
    Headers h = headers;
    QByteArray payload;
    if (body) {
        h.append(qMakePair(QByteArray("Content-Type"), QByteArray("application/json")));
        payload = body->isArray() ? QJsonDocument(body->toArray()).toJson(QJsonDocument::Compact) : QJsonDocument(body->toObject()).toJson(QJsonDocument::Compact);
    }
    const Response r = request(d, body ? QStringLiteral("POST") : QStringLiteral("GET"), pathAndQuery, h, payload, timeoutMs);
    JsonResponse out;
    out.status = r.status;
    out.certError = r.certError;
    out.error = r.error;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(r.body, &pe);
    if (pe.error == QJsonParseError::NoError) out.json = doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
    return out;
}

CertInfo readCertificate(const QString &host, int port, int timeoutMs)
{
    QSslSocket s;
    s.setPeerVerifyMode(QSslSocket::VerifyNone);
    bool hadErrors = false;
    QObject::connect(&s, &QSslSocket::sslErrors, &s, [&](const QList<QSslError> &) { hadErrors = true; s.ignoreSslErrors(); });
    s.connectToHostEncrypted(host, quint16(port));
    CertInfo info;
    if (!s.waitForEncrypted(timeoutMs)) return info;
    const QSslCertificate c = s.peerCertificate();
    if (c.isNull()) return info;
    info.ok = true;
    info.pem = QString::fromLatin1(c.toPem());
    info.fingerprint = fingerprintOf(c);
    info.valid = !hadErrors;
    s.disconnectFromHost();
    return info;
}

QString normalizeFingerprint(const QString &h)
{
    QString o;
    for (const QChar c : h) if (c.isDigit() || (c.toLower() >= QLatin1Char('a') && c.toLower() <= QLatin1Char('f'))) o += c.toUpper();
    return o;
}

bool fingerprintMatches(const QString &a, const QString &b) { return normalizeFingerprint(a) == normalizeFingerprint(b); }

ProbeResult probe(const Dest &d)
{
    ProbeResult r;
    r.dest = d;
    const JsonResponse j = requestJson(d, QStringLiteral("/api/v1/health"), {}, nullptr, 5000);
    if (j.status != 0) {
        r.type = (j.status == 200 && isHealth(j.json.toObject())) ? QStringLiteral("ok") : QStringLiteral("no-cinemalove");
        if (r.type == QLatin1String("ok")) r.health = j.json.toObject();
        return r;
    }
    if (!d.https || !j.certError) { r.type = QStringLiteral("sin-respuesta"); return r; }
    // Certificado que no se puede validar: se lee para mostrar su huella (no se confía todavía).
    const CertInfo c = readCertificate(d.host, d.port);
    if (!c.ok) { r.type = QStringLiteral("sin-respuesta"); return r; }
    r.fingerprint = c.fingerprint;
    r.pem = c.pem;
    if (!d.certPem.isEmpty()) { r.type = QStringLiteral("certificado-cambiado"); return r; }
    // Con ese certificado y solo para esta lectura, se identifica al servidor (nombre) para mostrarlo en el aviso.
    Dest pinned = d;
    pinned.certPem = c.pem;
    const JsonResponse h2 = requestJson(pinned, QStringLiteral("/api/v1/health"), {}, nullptr, 5000);
    if (h2.status == 200 && isHealth(h2.json.toObject())) { r.type = QStringLiteral("certificado-no-confiable"); r.health = h2.json.toObject(); }
    else r.type = QStringLiteral("no-cinemalove");
    return r;
}

} // namespace Remote
