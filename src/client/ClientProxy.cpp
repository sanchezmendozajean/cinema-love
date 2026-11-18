#include "ClientProxy.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSslCertificate>
#include <QUrlQuery>

#include "../server/HttpServer.h"
#include "Personal.h"

namespace {

QString randomToken(int bytes)
{
    QByteArray b(bytes, 0);
    for (int i = 0; i < bytes; ++i) b[i] = char(QRandomGenerator::system()->bounded(256));
    return QString::fromLatin1(b.toHex());
}

QNetworkAccessManager &nam()
{
    static thread_local QNetworkAccessManager *n = nullptr;
    if (!n) n = new QNetworkAccessManager();
    return *n;
}

/** Cabeceras que no se reenvían tal cual entre las dos conexiones. */
bool skipHeader(const QByteArray &k)
{
    static const QSet<QByteArray> skip = {"connection", "keep-alive", "transfer-encoding", "proxy-authenticate", "proxy-authorization", "te", "trailer", "upgrade", "host", "content-length"};
    const QByteArray l = k.toLower();
    return skip.contains(l) || l.startsWith("access-control-");
}

HttpResponse failure(const QString &serverName, bool certErr, bool https)
{
    const bool cert = certErr && https;
    return HttpResponse::json(QJsonObject{{"error", cert ? QStringLiteral("El certificado de «%1» cambió. Vuelve a conectarte para confiar en el nuevo.").arg(serverName) : QStringLiteral("No pudimos conectar con «%1».").arg(serverName)},
                                          {"codigo", cert ? "certificado-cambiado" : "sin-conexion"}}, 502);
}

}

ClientProxy::ClientProxy(Options o) : m_o(std::move(o)) {}
ClientProxy::~ClientProxy() { stop(); }

void ClientProxy::stop()
{
    if (m_http) { m_http->stop(); m_http.reset(); }
}

bool ClientProxy::start()
{
    m_token = randomToken(24);
    m_http = std::make_unique<HttpServer>();
    HttpServer &h = *m_http;
    const QString token = m_token;
    const Options o = m_o;

    h.setGuard([token](HttpRequest &req, HttpResponse &res) {
        if (req.path == QLatin1String("/api/v1/health")) return true;
        QString cred = req.headers.value(QStringLiteral("x-api-key"));
        if (cred.isEmpty()) cred = req.q(QStringLiteral("api_key"));
        if (cred != token) { res = HttpResponse::error(401, QStringLiteral("Credencial inválida o faltante.")); return false; }
        return true;
    });

    h.routePrefix(QString(), QStringLiteral("/api/v1/"), [o, &h](HttpRequest &req, std::function<void(HttpResponse)> done) {
        // La administración de un servidor ajeno no se maneja desde aquí (y el servidor la rechazaría de todos modos).
        if (req.path.startsWith(QLatin1String("/api/v1/admin/"))) { done(HttpResponse::error(404, QStringLiteral("Ruta no encontrada."))); return; }
        const bool isPublic = req.path == QLatin1String("/api/v1/health");
        const QString route = req.path.mid(int(QStringLiteral("/api/v1").size()));

        QUrlQuery query = req.query;
        query.removeAllQueryItems(QStringLiteral("api_key"));
        const QString queryText = query.isEmpty() ? QString() : QLatin1Char('?') + query.toString(QUrl::FullyEncoded);

        Remote::Headers headers;
        for (const QString &k : {QStringLiteral("range"), QStringLiteral("accept"), QStringLiteral("content-type"), QStringLiteral("user-agent"), QStringLiteral("if-none-match"),
                                 QStringLiteral("if-modified-since"), QStringLiteral("accept-language")})
            if (req.headers.contains(k)) headers.append({k.toLatin1(), req.headers.value(k).toUtf8()});
        if (!isPublic) headers.append({"x-api-key", o.credential().toUtf8()});

        // ---- Con token, lo personal se resuelve en este equipo (o se corrige encima de la respuesta del servidor)
        const Personal::Kind kind = o.personal ? Personal::classify(req.method, route) : Personal::Kind::None;
        if (kind != Personal::Kind::None) {
            const QString method = req.method;
            const QByteArray bodyBytes = req.body;
            h.runBlocking([o, kind, method, route, query, queryText, headers, bodyBytes]() -> HttpResponse {
                Personal::Reader reader = [&](const QString &path, const QJsonValue *body) -> QJsonValue {
                    const Remote::JsonResponse r = Remote::requestJson(o.dest, QStringLiteral("/api/v1") + path, {{"x-api-key", o.credential().toUtf8()}}, body, 20000);
                    if (r.status == 401 && o.onUnauthorized) o.onUnauthorized();
                    if (r.status == 0 && o.onFailure) o.onFailure(r.certError && o.dest.https ? QStringLiteral("certificado-cambiado") : QStringLiteral("sin-conexion"));
                    return r.status == 200 ? r.json : QJsonValue(QJsonValue::Null);
                };
                Personal personal(*o.personal, reader);
                if (kind == Personal::Kind::Local) {
                    QJsonValue body;
                    if (!bodyBytes.isEmpty()) {
                        QJsonParseError pe;
                        const QJsonDocument d = QJsonDocument::fromJson(bodyBytes, &pe);
                        if (pe.error != QJsonParseError::NoError) return HttpResponse::error(400, QStringLiteral("El cuerpo no es JSON válido."));
                        body = d.isArray() ? QJsonValue(d.array()) : QJsonValue(d.object());
                    }
                    const PersonalResponse r = personal.handle(method, route, query, body);
                    return HttpResponse::json(r.body, r.status);
                }
                if (kind == Personal::Kind::Query) {
                    const PersonalResponse r = personal.query(route, query);
                    return HttpResponse::json(r.body, r.status);
                }
                // Overlay: se pide al servidor y se corrige encima con el progreso de aquí
                const Remote::JsonResponse r = Remote::requestJson(o.dest, QStringLiteral("/api/v1") + route + queryText, headers, nullptr, 30000);
                if (r.status == 401 && o.onUnauthorized) o.onUnauthorized();
                if (r.status == 0) { if (o.onFailure) o.onFailure(r.certError && o.dest.https ? QStringLiteral("certificado-cambiado") : QStringLiteral("sin-conexion")); return failure(o.serverName, r.certError, o.dest.https); }
                if (r.status != 200 || r.json.isNull()) return HttpResponse::json(r.json.isNull() ? QJsonValue(QJsonObject{{"error", "Respuesta inválida del servidor."}}) : r.json, r.status ? r.status : 502);
                return HttpResponse::json(personal.overlay(r.json));
            }, done);
            return;
        }

        // ---- Reenvío tal cual, con el cuerpo transmitido por trozos (a veces un video de gigas)
        QNetworkRequest nr{QUrl(Remote::urlOf(o.dest) + req.path + queryText)};
        nr.setTransferTimeout(30000);
        for (const auto &hd : headers) nr.setRawHeader(hd.first, hd.second);
        QSslCertificate pinned;
        if (o.dest.https && !o.dest.certPem.isEmpty()) pinned = QSslCertificate(o.dest.certPem.toLatin1(), QSsl::Pem);
        const QByteArray method = req.method.toLatin1();
        QNetworkReply *rep = method == "GET" ? nam().get(nr) : nam().sendCustomRequest(nr, method, req.body);
        rep->setReadBufferSize(1024 * 1024);
        auto certErr = std::make_shared<bool>(false);
        if (o.dest.https)
            QObject::connect(rep, &QNetworkReply::sslErrors, rep, [rep, pinned, certErr](const QList<QSslError> &) {
                const QSslCertificate peer = rep->sslConfiguration().peerCertificate();
                if (!pinned.isNull() && !peer.isNull() && peer.digest(QCryptographicHash::Sha256) == pinned.digest(QCryptographicHash::Sha256)) rep->ignoreSslErrors();
                else *certErr = true;
            });
        if (o.dest.https)
            QObject::connect(rep, &QNetworkReply::encrypted, rep, [rep, pinned, certErr] {
                if (pinned.isNull() || rep->sslConfiguration().peerCertificate().digest(QCryptographicHash::Sha256) != pinned.digest(QCryptographicHash::Sha256)) { *certErr = true; rep->abort(); }
            });
        auto answered = std::make_shared<bool>(false);
        auto begin = [o, rep, done, answered, isPublic]() {
            if (*answered) return;
            const int status = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 0) return;
            *answered = true;
            HttpResponse res;
            res.status = status;
            res.stream = [o, rep, status, isPublic](ResponseWriter *w) {
                QList<QPair<QByteArray, QByteArray>> out;
                for (const auto &p : rep->rawHeaderPairs()) if (!skipHeader(p.first)) out.append(p);
                const QVariant len = rep->header(QNetworkRequest::ContentLengthHeader);
                w->begin(status, out, len.isValid() ? len.toLongLong() : -1);
                auto pump = [rep, w]() {
                    while (w->isOpen() && rep->bytesAvailable() > 0 && w->bytesToWrite() < 1024 * 1024) w->write(rep->read(256 * 1024));
                    if (w->isOpen() && rep->isFinished() && rep->bytesAvailable() == 0) w->end();
                };
                QObject::connect(rep, &QNetworkReply::readyRead, w, pump);
                QObject::connect(rep, &QNetworkReply::finished, w, pump);
                QObject::connect(w, &ResponseWriter::drained, rep, pump);
                QObject::connect(w, &ResponseWriter::closed, rep, [rep] { rep->abort(); });
                // Se avisa cuando la respuesta 401 ya salió hacia la interfaz: avisar antes cerraría el proxy a mitad de la entrega.
                if (status == 401 && !isPublic && o.onUnauthorized) QObject::connect(rep, &QNetworkReply::finished, rep, [o] { o.onUnauthorized(); });
                QObject::connect(rep, &QNetworkReply::finished, rep, [rep] { rep->deleteLater(); });
                pump();
            };
            done(res);
        };
        QObject::connect(rep, &QNetworkReply::metaDataChanged, rep, begin);
        QObject::connect(rep, &QNetworkReply::finished, rep, [o, rep, done, answered, certErr, begin] {
            begin();
            if (*answered) return;
            *answered = true;
            const bool cancelled = rep->error() == QNetworkReply::OperationCanceledError;
            if (o.onFailure && !cancelled) o.onFailure(*certErr && o.dest.https ? QStringLiteral("certificado-cambiado") : QStringLiteral("sin-conexion"));
            done(failure(o.serverName, *certErr, o.dest.https));
            rep->deleteLater();
        });
    });

    const quint16 port = h.listen(QStringLiteral("127.0.0.1"), 0);
    if (!port) { m_http.reset(); return false; }
    m_baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
    return true;
}
