#include "PublicServer.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QNetworkInterface>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSysInfo>
#include <QTcpServer>

#include "ApiServer.h"
#include "Activity.h"
#include "Certs.h"
#include "Qr.h"
#include "HttpServer.h"
#include "Store.h"

namespace {

// Claves heredadas de Flutter (`server_*`) y propias (`cl.*`) de `app_settings` (specs/heredadas/02 §5).
const QString kMode = QStringLiteral("server_mode"), kHttp = QStringLiteral("server_http_port"), kHttps = QStringLiteral("server_https_port"),
              kName = QStringLiteral("cl.server_name"), kAuto = QStringLiteral("cl.server_autostart"), kMdns = QStringLiteral("cl.discovery_enabled");
constexpr int kMinPort = 1024;

QString setting(const QString &key) { return Store::scalar(QStringLiteral("SELECT valor FROM app_settings WHERE clave = ?"), {key}).toString(); }
void saveSetting(const QString &key, const QString &value)
{
    Store::exec(QStringLiteral("INSERT INTO app_settings (clave, valor) VALUES (?, ?) ON CONFLICT(clave) DO UPDATE SET valor = excluded.valor"), {key, value});
}

int portSetting(const QString &key, int fallback)
{
    bool ok = false;
    const int n = setting(key).toInt(&ok);
    return ok && n >= 0 && n <= 65535 ? n : fallback;
}

/** IPv4 de las interfaces de red, con las privadas (192.168/10/172.16) primero. */
QStringList lanAddresses()
{
    QStringList ips;
    for (const QNetworkInterface &i : QNetworkInterface::allInterfaces()) {
        if (!(i.flags() & QNetworkInterface::IsUp) || !(i.flags() & QNetworkInterface::IsRunning) || (i.flags() & QNetworkInterface::IsLoopBack)) continue;
        for (const QNetworkAddressEntry &e : i.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol) ips << e.ip().toString();
    }
    auto priv = [](const QString &ip) {
        static const QRegularExpression re(QStringLiteral(R"(^(192\.168\.|10\.|172\.(1[6-9]|2\d|3[01])\.))"));
        return re.match(ip).hasMatch() ? 0 : 1;
    };
    std::stable_sort(ips.begin(), ips.end(), [&](const QString &a, const QString &b) { return priv(a) < priv(b); });
    return ips;
}

/** ¿Se puede escuchar en este puerto en todas las interfaces? */
bool portFree(int port)
{
    QTcpServer s;
    const bool ok = s.listen(QHostAddress::AnyIPv4, quint16(port));
    s.close();
    return ok;
}

} // namespace

PublicServer::PublicServer() = default;
PublicServer::~PublicServer() { stop(); }

QString PublicServer::serverName()
{
    const QString n = setting(kName).trimmed();
    return n.isEmpty() ? QSysInfo::machineHostName() : n;
}

QJsonObject PublicServer::config() const
{
    return QJsonObject{{"protocolo", setting(kMode) == QLatin1String("https") ? "https" : "http"}, {"puertoHttp", portSetting(kHttp, 8080)}, {"puertoHttps", portSetting(kHttps, 8443)},
                       {"nombre", serverName()}, {"mdns", setting(kMdns) != QLatin1String("false")}, {"autostart", setting(kAuto) == QLatin1String("true")}};
}

QJsonObject PublicServer::state() const
{
    const QJsonObject c = config();
    const QString protocol = active() ? m_runProtocol : c.value(QStringLiteral("protocolo")).toString();
    const int port = active() ? m_runPort : (protocol == QLatin1String("https") ? c.value(QStringLiteral("puertoHttps")).toInt() : c.value(QStringLiteral("puertoHttp")).toInt());
    const QString fingerprint = Certs::Storage(Store::folder()).load().fingerprint;
    QJsonArray ips, urls;
    for (const QString &ip : lanAddresses()) { ips.append(ip); urls.append(QStringLiteral("%1://%2:%3").arg(protocol, ip).arg(port)); }
    return QJsonObject{{"activo", active()}, {"protocolo", protocol}, {"puerto", port}, {"nombre", c.value(QStringLiteral("nombre")).toString()}, {"direcciones", ips}, {"urls", urls},
                       {"huella", fingerprint.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(fingerprint)}, {"anunciando", m_mdns.announcing()}};
}

/** Guarda la configuración. Protocolo y puertos solo con el servidor detenido; el puerto debe estar libre. */
void PublicServer::saveConfig(const QJsonObject &c)
{
    const QJsonObject cur = config();
    auto changed = [&](const QString &k) { return c.contains(k) && c.value(k) != cur.value(k); };
    if (active() && (changed(QStringLiteral("protocolo")) || changed(QStringLiteral("puertoHttp")) || changed(QStringLiteral("puertoHttps"))))
        throw Error(QStringLiteral("Detén el servidor para cambiar el protocolo o el puerto."), 409);
    if (c.contains(QStringLiteral("protocolo")) && c.value(QStringLiteral("protocolo")).toString() != QLatin1String("http") && c.value(QStringLiteral("protocolo")).toString() != QLatin1String("https"))
        throw Error(QStringLiteral("Protocolo inválido."));
    for (const auto &pr : {qMakePair(QStringLiteral("HTTP"), QStringLiteral("puertoHttp")), qMakePair(QStringLiteral("HTTPS"), QStringLiteral("puertoHttps"))}) {
        if (!c.contains(pr.second)) continue;
        const QJsonValue v = c.value(pr.second);
        const double n = v.toDouble(-1);
        if (!v.isDouble() || n != int(n) || n < kMinPort || n > 65535) throw Error(QStringLiteral("El puerto %1 debe estar entre %2 y 65535.").arg(pr.first).arg(kMinPort));
        if (int(n) != cur.value(pr.second).toInt() && !portFree(int(n))) throw Error(QStringLiteral("El puerto %1 ya está en uso por otro programa.").arg(int(n)), 409);
    }
    if (c.contains(QStringLiteral("nombre")) && (!c.value(QStringLiteral("nombre")).isString() || c.value(QStringLiteral("nombre")).toString().trimmed().isEmpty() || c.value(QStringLiteral("nombre")).toString().trimmed().size() > 60))
        throw Error(QStringLiteral("El nombre del servidor debe tener entre 1 y 60 caracteres."));
    if (c.contains(QStringLiteral("protocolo"))) saveSetting(kMode, c.value(QStringLiteral("protocolo")).toString());
    if (c.contains(QStringLiteral("puertoHttp"))) saveSetting(kHttp, QString::number(c.value(QStringLiteral("puertoHttp")).toInt()));
    if (c.contains(QStringLiteral("puertoHttps"))) saveSetting(kHttps, QString::number(c.value(QStringLiteral("puertoHttps")).toInt()));
    if (c.contains(QStringLiteral("nombre"))) saveSetting(kName, c.value(QStringLiteral("nombre")).toString().trimmed());
    if (c.contains(QStringLiteral("mdns"))) saveSetting(kMdns, c.value(QStringLiteral("mdns")).toBool() ? QStringLiteral("true") : QStringLiteral("false"));
    if (c.contains(QStringLiteral("autostart"))) saveSetting(kAuto, c.value(QStringLiteral("autostart")).toBool() ? QStringLiteral("true") : QStringLiteral("false"));
    if (active()) updateAnnouncement();   // el nombre y mDNS sí cambian en caliente
}

void PublicServer::updateAnnouncement()
{
    if (!active()) return;
    const QJsonObject c = config();
    if (c.value(QStringLiteral("mdns")).toBool()) {
        const QString id = Store::scalar(QStringLiteral("SELECT valor FROM cl_meta WHERE clave = 'server_id'")).toString();
        const QString name = c.value(QStringLiteral("nombre")).toString();
        m_mdns.publish(name, m_runPort, {{QStringLiteral("v"), QStringLiteral("1")}, {QStringLiteral("id"), id}, {QStringLiteral("name"), name},
                                         {QStringLiteral("tls"), m_runProtocol == QLatin1String("https") ? QStringLiteral("1") : QStringLiteral("0")}});
    } else {
        m_mdns.unpublish();
    }
}

void PublicServer::start()
{
    if (active()) return;
    const QJsonObject c = config();
    const QString protocol = c.value(QStringLiteral("protocolo")).toString();
    const bool https = protocol == QLatin1String("https");
    const int port = c.value(https ? QStringLiteral("puertoHttps") : QStringLiteral("puertoHttp")).toInt();
    auto http = std::make_unique<HttpServer>();
    if (https) {
        const Certs::Certificate cert = Certs::Storage(Store::folder()).loadOrGenerate(c.value(QStringLiteral("nombre")).toString(), lanAddresses());
        QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
        const QSslCertificate certificate(cert.certPem, QSsl::Pem);
        const QSslKey key(cert.keyPem, QSsl::Rsa, QSsl::Pem);
        if (certificate.isNull() || key.isNull()) throw Error(QStringLiteral("No se pudo cargar el certificado del servidor. Regéneralo en Ajustes → Servidor."), 500);
        tls.setLocalCertificate(certificate);
        tls.setPrivateKey(key);
        tls.setPeerVerifyMode(QSslSocket::VerifyNone);
        http->setTls(tls);
    }
    http->setThreadDone([] { Store::closeThreadConnection(); });
    ApiServer::installRoutes(*http, QString(), nullptr);   // sin token admin ni administración: solo la API de cliente
    const quint16 listening = http->listen(QStringLiteral("0.0.0.0"), quint16(port));
    if (!listening) throw Error(QStringLiteral("El puerto %1 ya está en uso por otro programa. Elige otro en Ajustes → Servidor.").arg(port), 409);
    m_http = std::move(http);
    m_runProtocol = protocol;
    m_runPort = listening;
    updateAnnouncement();
}

void PublicServer::stop()
{
    if (!m_http) return;
    m_mdns.unpublish();
    m_http->stop();
    m_http.reset();
}

/** Genera un certificado nuevo. Si el servidor estaba corriendo en HTTPS se reinicia para usarlo (los clientes tendrán que volver a confiar). */
void PublicServer::regenerateCertificate()
{
    const bool restart = active() && m_runProtocol == QLatin1String("https");
    if (restart) stop();
    Certs::Storage(Store::folder()).regenerate(serverName(), lanAddresses());
    if (restart) start();
}

void registerServerAdminRoutes(HttpServer &h, PublicServer &ps)
{
    const QString A = QStringLiteral("/api/v1/admin");
    auto wrap = [](auto &&fn) {
        try { return fn(); }
        catch (const PublicServer::Error &e) { return HttpResponse::error(e.status, QString::fromUtf8(e.what())); }
    };
    h.route("GET", A + "/servidor", [&ps](HttpRequest &) { return HttpResponse::json(ps.view()); });
    h.route("PUT", A + "/servidor/config", [&ps, wrap](HttpRequest &r) {
        return wrap([&] {
            bool ok = false;
            const QJsonValue v = r.json(&ok);
            ps.saveConfig(ok && v.isObject() ? v.toObject() : QJsonObject());
            return HttpResponse::json(ps.view());
        });
    });
    h.route("POST", A + "/servidor/iniciar", [&ps, wrap](HttpRequest &) { return wrap([&] { ps.start(); return HttpResponse::json(ps.view()); }); });
    h.route("POST", A + "/servidor/detener", [&ps, wrap](HttpRequest &) { return wrap([&] { ps.stop(); return HttpResponse::json(ps.view()); }); });
    h.route("POST", A + "/servidor/certificado/regenerar", [&ps, wrap](HttpRequest &) { return wrap([&] { ps.regenerateCertificate(); return HttpResponse::json(ps.view()); }); });
    h.route("GET", A + "/servidor/qr", [](HttpRequest &r) {
        static const QRegularExpression ok(QStringLiteral(R"(^https?://\S+$)"));
        const QString url = r.q(QStringLiteral("url"));
        if (!ok.match(url).hasMatch() || url.size() > 200) return HttpResponse::error(400, QStringLiteral("Dirección inválida."));
        const QString svg = Qr::svg(url, 1);
        return svg.isEmpty() ? HttpResponse::error(400, QStringLiteral("Dirección inválida.")) : HttpResponse::json(QJsonObject{{"svg", svg}});
    });
    // Sesiones activas: el streaming directo no abre sesión y no hay conversión en esta versión.
    h.route("GET", A + "/sesiones", [](HttpRequest &) { return HttpResponse::json(QJsonObject{{"conversiones", QJsonArray()}, {"directas", Activity::active()}}); });
    h.route("DELETE", A + "/sesiones/:id", [](HttpRequest &) { return HttpResponse::error(404, QStringLiteral("La sesión ya terminó.")); });
}
