#include "ClientBackend.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QRandomGenerator>
#include <QtConcurrent>

#include "../server/HttpServer.h"
#include "../server/Mdns.h"
#include "ClientConnection.h"
#include "ClientDb.h"
#include "Mirror.h"

namespace {

QString randomToken(int bytes)
{
    QByteArray b(bytes, 0);
    for (int i = 0; i < bytes; ++i) b[i] = char(QRandomGenerator::system()->bounded(256));
    return QString::fromLatin1(b.toHex());
}

QMutex g_mirrorMutex;

QJsonObject bodyObj(const HttpRequest &r)
{
    bool ok = false;
    const QJsonValue v = r.json(&ok);
    return ok && v.isObject() ? v.toObject() : QJsonObject();
}

}

ClientBackend::ClientBackend() = default;
ClientBackend::~ClientBackend() { stop(); }

QString ClientBackend::start(const QString &folder)
{
    stop();
    m_folder = QDir(folder).absolutePath();
    QDir().mkpath(m_folder);
    m_db = std::make_unique<ClientDb>(m_folder + QStringLiteral("/cliente_datos.sqlite"));
    const QString err = m_db->open();
    if (!err.isEmpty()) { m_db.reset(); return err; }
    m_conn = std::make_unique<ClientConnection>(*m_db);
    m_token = randomToken(24);
    m_http = std::make_unique<HttpServer>();
    HttpServer &h = *m_http;
    const QString token = m_token;
    h.setGuard([token](HttpRequest &req, HttpResponse &res) {
        if (req.method == QLatin1String("OPTIONS")) return true;
        if (req.headers.value(QStringLiteral("x-api-key")) != token) { res = HttpResponse::error(401, QStringLiteral("No autorizado.")); return false; }
        return true;
    });

    h.route("GET", "/control/estado", [this](HttpRequest &) {
        const Mirror::State s = m_mirror ? m_mirror->state() : Mirror::State();
        return HttpResponse::json(QJsonObject{{"conexion", m_conn->state()},
                                              {"espejo", QJsonObject{{"fase", s.phase}, {"hecho", s.done}, {"total", s.total}, {"ruta", s.path.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(s.path)}, {"mensaje", s.message}}},
                                              {"recordados", m_conn->remembered()}});
    });
    h.routeAsync("POST", "/control/probar", [this, &h](HttpRequest &r, std::function<void(HttpResponse)> done) {
        const QString url = bodyObj(r).value(QStringLiteral("url")).toString();
        h.runBlocking([this, url] { return HttpResponse::json(m_conn->probe(url)); }, done);
    });
    h.routeAsync("POST", "/control/conectar", [this, &h](HttpRequest &r, std::function<void(HttpResponse)> done) {
        const QJsonObject body = bodyObj(r);
        h.runBlocking([this, body] {
            const QJsonObject res = m_conn->connectTo(body);
            if (res.value(QStringLiteral("ok")).toBool()) launchMirror();
            return HttpResponse::json(res);
        }, done);
    });
    h.routeAsync("POST", "/control/reconectar", [this, &h](HttpRequest &r, std::function<void(HttpResponse)> done) {
        const QString id = bodyObj(r).value(QStringLiteral("id")).toString();
        h.runBlocking([this, id] {
            const QJsonObject res = m_conn->reconnect(id);
            if (res.value(QStringLiteral("ok")).toBool()) launchMirror();
            return HttpResponse::json(res);
        }, done);
    });
    h.routeAsync("POST", "/control/renovar", [this, &h](HttpRequest &r, std::function<void(HttpResponse)> done) {
        const QString pw = bodyObj(r).value(QStringLiteral("password")).toString();
        h.runBlocking([this, pw] {
            const QJsonObject res = m_conn->renewSession(pw);
            if (res.value(QStringLiteral("ok")).toBool()) launchMirror();
            return HttpResponse::json(res);
        }, done);
    });
    h.routeAsync("POST", "/control/confiar", [this, &h](HttpRequest &r, std::function<void(HttpResponse)> done) {
        const QString fp = bodyObj(r).value(QStringLiteral("huella")).toString();
        h.runBlocking([this, fp] {
            const QJsonObject res = m_conn->trustNewCertificate(fp);
            if (res.value(QStringLiteral("ok")).toBool()) launchMirror();
            return HttpResponse::json(res);
        }, done);
    });
    h.routeAsync("POST", "/control/reintentar", [this, &h](HttpRequest &, std::function<void(HttpResponse)> done) {
        h.runBlocking([this] {
            const QJsonObject res = m_conn->retry();
            launchMirror();
            return HttpResponse::json(res);
        }, done);
    });
    h.route("POST", "/control/refrescar", [this](HttpRequest &) { launchMirror(); return HttpResponse::json(QJsonObject{{"ok", true}}); });
    h.routeAsync("POST", "/control/personal", [this, &h](HttpRequest &, std::function<void(HttpResponse)> done) {
        std::shared_ptr<Mirror> m;
        { QMutexLocker l(&g_mirrorMutex); m = m_mirror; }
        h.runBlocking([m] { if (m) m->updatePersonal(); return HttpResponse::json(QJsonObject{{"ok", true}}); }, done);
    });
    // Descubrimiento en la red local: la primera llamada empieza a buscar; después devuelve lo visto hasta ahora.
    h.route("GET", "/control/descubrir", [this](HttpRequest &) {
        if (!m_mdns) { m_mdns = std::make_unique<Mdns>(); m_mdns->startBrowse(); }
        QJsonArray out;
        for (const Mdns::Found &f : m_mdns->servers())
            out.append(QJsonObject{{"id", f.id}, {"nombre", f.name}, {"host", f.host}, {"puerto", f.port}, {"tls", f.tls}});
        return HttpResponse::json(QJsonObject{{"servidores", out}});
    });
    h.route("POST", "/control/descubrir/detener", [this](HttpRequest &) { m_mdns.reset(); return HttpResponse::json(QJsonObject{{"ok", true}}); });
    h.routeAsync("POST", "/control/desconectar", [this, &h](HttpRequest &, std::function<void(HttpResponse)> done) {
        h.runBlocking([this] { stopMirror(); m_conn->disconnect(); return HttpResponse::json(QJsonObject{{"ok", true}}); }, done);
    });
    h.routeAsync("POST", "/control/olvidar", [this, &h](HttpRequest &, std::function<void(HttpResponse)> done) {
        h.runBlocking([this] { stopMirror(); m_conn->forget(); return HttpResponse::json(QJsonObject{{"ok", true}}); }, done);
    });

    const quint16 port = h.listen(QStringLiteral("127.0.0.1"), 0);
    if (!port) return QStringLiteral("No se pudo iniciar el servicio del Modo Cliente.");
    m_url = QStringLiteral("http://127.0.0.1:%1").arg(port);
    return {};
}

void ClientBackend::stopMirror()
{
    std::shared_ptr<Mirror> m;
    { QMutexLocker l(&g_mirrorMutex); m = std::move(m_mirror); }
    if (m) m->cancel();
}

/** Lanza (o relanza) la construcción del espejo cuando hay una conexión activa. */
void ClientBackend::launchMirror()
{
    stopMirror();
    const QJsonObject st = m_conn->state();
    if (st.value(QStringLiteral("tipo")).toString() != QLatin1String("conectada")) return;
    const QJsonObject api = st.value(QStringLiteral("api")).toObject();
    const QString serverId = st.value(QStringLiteral("info")).toObject().value(QStringLiteral("servidorId")).toString();
    auto mirror = std::make_shared<Mirror>(m_folder, api.value(QStringLiteral("baseUrl")).toString(), api.value(QStringLiteral("token")).toString(), serverId);
    { QMutexLocker l(&g_mirrorMutex); m_mirror = mirror; }
    (void)QtConcurrent::run([mirror] { mirror->build(); });
}

void ClientBackend::stop()
{
    stopMirror();
    if (m_conn) m_conn->setListener({});
    if (m_http) { m_http->stop(); m_http.reset(); }
    m_mdns.reset();
    m_conn.reset();   // cierra el proxy y la conexión
    m_db.reset();
    m_url.clear();
}
