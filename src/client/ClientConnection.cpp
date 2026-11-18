#include "ClientConnection.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QtConcurrent>

namespace {

constexpr qint64 kSessionSeconds = 30 * 24 * 3600;   // duración de una sesión del servidor (specs/03 §2)

QMutex g_opMutex;   // una operación de conexión a la vez

QJsonValue jstr(const QString &s) { return s.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(s); }

struct Verified { Remote::Dest dest; QJsonObject health; QString fingerprint, pem; };

struct Cred { QString value, user; qint64 expires = 0; };

}

ClientConnection::ClientConnection(const ClientDb &db) : m_db(db), m_servers(db) {}

ClientConnection::~ClientConnection()
{
    closeActive();
    if (m_sessionDb) {
        m_sessionDb->closeThreadConnection();
        m_sessionDb.reset();
        for (const QString &ext : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm"), QStringLiteral("-journal")}) QFile::remove(m_sessionDbPath + ext);
    }
}

QJsonObject ClientConnection::state() const
{
    QMutexLocker l(&m_mutex);
    return m_state;
}

void ClientConnection::setState(const QJsonObject &s)
{
    {
        QMutexLocker l(&m_mutex);
        m_state = s;
    }
    if (m_listener) m_listener(s);
}

void ClientConnection::closeActive()
{
    std::unique_ptr<Active> a;
    {
        QMutexLocker l(&m_mutex);
        a = std::move(m_active);
    }
    if (a && a->proxy) a->proxy->stop();
}

QJsonObject ClientConnection::wrap(const std::function<QJsonObject()> &fn)
{
    try {
        return QJsonObject{{"ok", true}, {"estado", fn()}};
    } catch (const ClientError &e) {
        QJsonObject o{{"ok", false}, {"codigo", e.code}, {"mensaje", QString::fromUtf8(e.what())}};
        if (!e.fingerprint.isEmpty()) o.insert(QStringLiteral("huella"), e.fingerprint);
        if (!e.serverName.isEmpty()) o.insert(QStringLiteral("nombreServidor"), e.serverName);
        return o;
    }
}

// ----------------------------------------------------------------- verificación

QJsonObject ClientConnection::probe(const QString &url)
{
    QMutexLocker op(&g_opMutex);
    auto dest = Remote::parseUrl(url);
    if (!dest) return QJsonObject{{"tipo", "url-invalida"}};
    const SavedServer saved = m_servers.byAddress(dest->host, dest->port);
    if (dest->https && saved.valid() && saved.trustCert && !saved.certPem.isEmpty()) dest->certPem = saved.certPem;
    const Remote::ProbeResult p = Remote::probe(*dest);
    const QString u = Remote::urlOf(p.dest);
    if (p.type == QLatin1String("ok"))
        return QJsonObject{{"tipo", "ok"}, {"nombre", p.health.value(QStringLiteral("nombre")).isString() ? p.health.value(QStringLiteral("nombre")).toString() : p.dest.host}, {"url", u}, {"https", p.dest.https}};
    if (p.type == QLatin1String("certificado-no-confiable"))
        return QJsonObject{{"tipo", p.type}, {"nombre", p.health.value(QStringLiteral("nombre")).isString() ? p.health.value(QStringLiteral("nombre")) : QJsonValue(QJsonValue::Null)}, {"url", u}, {"huella", p.fingerprint}};
    if (p.type == QLatin1String("certificado-cambiado"))
        return QJsonObject{{"tipo", p.type}, {"nombre", QJsonValue(QJsonValue::Null)}, {"url", u}, {"huella", p.fingerprint}};
    return QJsonObject{{"tipo", p.type}};
}

namespace {

Verified verified(const QString &url, const QString &trustFingerprint, const ClientConnection *, const Servers &servers)
{
    auto dest = Remote::parseUrl(url);
    if (!dest) throw ClientError(QStringLiteral("url-invalida"), QStringLiteral("La dirección no es válida. Escríbela como http://192.168.0.115:8080."));
    const SavedServer saved = servers.byAddress(dest->host, dest->port);
    if (dest->https && saved.valid() && saved.trustCert && !saved.certPem.isEmpty()) dest->certPem = saved.certPem;
    const Remote::ProbeResult s = Remote::probe(*dest);
    if (s.type == QLatin1String("sin-respuesta")) throw ClientError(QStringLiteral("sin-respuesta"), QStringLiteral("No hay respuesta en esa dirección. Revisa que el otro equipo esté encendido y comparta su colección."));
    if (s.type == QLatin1String("no-cinemalove")) throw ClientError(QStringLiteral("no-cinemalove"), QStringLiteral("Hay algo en esa dirección, pero no es un servidor CinemaLove."));
    if (s.type == QLatin1String("certificado-no-confiable") || s.type == QLatin1String("certificado-cambiado")) {
        const QString name = s.type == QLatin1String("certificado-no-confiable") ? s.health.value(QStringLiteral("nombre")).toString() : QString();
        if (trustFingerprint.isEmpty() || !Remote::fingerprintMatches(trustFingerprint, s.fingerprint)) {
            const QString msg = s.type == QLatin1String("certificado-cambiado")
                ? QStringLiteral("El certificado del servidor cambió desde la última vez. Confirma la nueva huella solo si fue el dueño quien lo regeneró.")
                : QStringLiteral("El servidor usa un certificado propio. Compara la huella con la que muestra en Ajustes → Servidor y confía solo si coincide.");
            throw ClientError(s.type, msg, s.fingerprint, name);
        }
        // Confirmado por la persona: desde ahora la cadena se valida contra ESE certificado exacto.
        Remote::Dest trusted = s.dest;
        trusted.certPem = s.pem;
        const Remote::ProbeResult r = Remote::probe(trusted);
        if (r.type != QLatin1String("ok")) throw ClientError(QStringLiteral("sin-respuesta"), QStringLiteral("No pudimos verificar el servidor con ese certificado."));
        return {trusted, r.health, s.fingerprint, s.pem};
    }
    return {s.dest, s.health, QString(), s.dest.certPem};
}

/** Autentica contra el servidor y devuelve la credencial que se usará (token de sesión o API key). */
Cred authenticate(const Remote::Dest &d, const QJsonObject &c)
{
    if (c.value(QStringLiteral("tipo")).toString() == QLatin1String("sesion")) {
        const QJsonValue body(QJsonObject{{"nombreUsuario", c.value(QStringLiteral("usuario"))}, {"password", c.value(QStringLiteral("password"))}});
        const Remote::JsonResponse r = Remote::requestJson(d, QStringLiteral("/api/v1/login"), {}, &body);
        if (r.status == 0) throw ClientError(QStringLiteral("sin-respuesta"), QStringLiteral("No hay respuesta en esa dirección. Revisa que el otro equipo esté encendido y comparta su colección."));
        if (r.status == 429) throw ClientError(QStringLiteral("demasiados-intentos"), QStringLiteral("Demasiados intentos. Espera un minuto e inténtalo de nuevo."));
        const QString token = r.json.toObject().value(QStringLiteral("token")).toString();
        if (r.status != 200 || token.isEmpty()) throw ClientError(QStringLiteral("credenciales"), QStringLiteral("Usuario o contraseña inválidos."));
        const QString user = r.json.toObject().value(QStringLiteral("nombreUsuario")).isString() ? r.json.toObject().value(QStringLiteral("nombreUsuario")).toString() : c.value(QStringLiteral("usuario")).toString();
        return {token, user, QDateTime::currentSecsSinceEpoch() + kSessionSeconds};
    }
    const QString key = c.value(QStringLiteral("apiKey")).toString();
    const Remote::JsonResponse r = Remote::requestJson(d, QStringLiteral("/api/v1/me"), {{"X-Api-Key", key.toUtf8()}});
    if (r.status == 0) throw ClientError(QStringLiteral("sin-respuesta"), QStringLiteral("No hay respuesta en esa dirección. Revisa que el otro equipo esté encendido y comparta su colección."));
    if (r.status != 200) throw ClientError(QStringLiteral("token-invalido"), QStringLiteral("El token de acceso no es válido (¿lo regeneraron en el servidor?)."));
    return {key, QString(), 0};
}

}

// ----------------------------------------------------------------- conectar

QJsonObject ClientConnection::connectTo(const QJsonObject &req)
{
    QMutexLocker op(&g_opMutex);
    return wrap([&]() -> QJsonObject {
        const Verified v = verified(req.value(QStringLiteral("url")).toString(), req.value(QStringLiteral("confiarHuella")).toString(), this, m_servers);
        const QJsonObject credObj = req.value(QStringLiteral("credencial")).toObject();
        const Cred cred = authenticate(v.dest, credObj);
        const QString name = v.health.value(QStringLiteral("nombre")).toString().trimmed().isEmpty() ? v.dest.host : v.health.value(QStringLiteral("nombre")).toString().trimmed();
        const QString type = credObj.value(QStringLiteral("tipo")).toString();
        const bool remember = req.value(QStringLiteral("recordar")).toBool();
        QString serverId;
        if (remember) {
            const SavedServer prev = m_servers.byAddress(v.dest.host, v.dest.port);
            QVariantMap d;
            d["credentialType"] = type;
            d["apiKey"] = type == QLatin1String("apiKey") ? QVariant(credObj.value(QStringLiteral("apiKey")).toString()) : QVariant(QMetaType::fromType<QString>());
            d["lastUser"] = cred.user.isEmpty() ? QVariant(QMetaType::fromType<QString>()) : QVariant(cred.user);
            d["sessionToken"] = type == QLatin1String("sesion") ? QVariant(cred.value) : QVariant(QMetaType::fromType<QString>());
            d["sessionExpires"] = cred.expires ? QVariant(cred.expires) : QVariant(QMetaType::fromType<QString>());
            d["trustCert"] = !v.pem.isEmpty();
            d["name"] = name;
            d["serverUuid"] = v.health.value(QStringLiteral("serverId")).isString() ? QVariant(v.health.value(QStringLiteral("serverId")).toString()) : QVariant(QMetaType::fromType<QString>());
            d["fingerprint"] = !v.fingerprint.isEmpty() ? QVariant(v.fingerprint) : (prev.fingerprint.isEmpty() ? QVariant(QMetaType::fromType<QString>()) : QVariant(prev.fingerprint));
            d["certPem"] = v.pem.isEmpty() ? QVariant(QMetaType::fromType<QString>()) : QVariant(v.pem);
            serverId = m_servers.save(v.dest.host, v.dest.port, v.dest.https, d).id;
        }
        return activate(serverId, name, v.dest, type, cred.user, cred.value, remember);
    });
}

QJsonObject ClientConnection::activate(const QString &serverId, const QString &name, const Remote::Dest &dest, const QString &credential, const QString &user, const QString &value, bool remembered)
{
    closeActive();
    auto a = std::make_unique<Active>();
    a->serverId = serverId;
    a->name = name;
    a->user = user;
    a->dest = dest;
    a->info = QJsonObject{{"servidorId", jstr(serverId)}, {"nombre", name}, {"url", Remote::urlOf(dest)}, {"credencial", credential}, {"usuario", jstr(user)}, {"recordada", remembered}};

    // Con token no hay un usuario en el servidor: el progreso, «Próximas a ver» y las colecciones viven en este equipo.
    // Si el servidor no se recuerda, solo duran lo que dure la sesión.
    std::optional<LocalState> local;
    if (credential == QLatin1String("apiKey")) {
        if (!serverId.isEmpty()) local.emplace(m_db, serverId);
        else {
            if (!m_sessionDb) {
                m_sessionDbPath = QDir::temp().filePath(QStringLiteral("cinemalove-sesion-%1.sqlite").arg(QCoreApplication::applicationPid()));
                for (const QString &ext : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm")}) QFile::remove(m_sessionDbPath + ext);
                auto sdb = std::make_unique<ClientDb>(m_sessionDbPath);
                const QString err = sdb->open();
                if (!err.isEmpty()) throw ClientError(QStringLiteral("sin-conexion"), err);
                sdb->exec(QStringLiteral("INSERT INTO cliente_servidores (id, host, puerto) VALUES ('sesion', 'sesion', 0)"));
                m_sessionDb = std::move(sdb);
            }
            local.emplace(*m_sessionDb, QStringLiteral("sesion"));
        }
    }
    Active *raw = a.get();
    ClientProxy::Options po;
    po.dest = dest;
    po.credential = [value] { return value; };
    po.serverName = name;
    po.personal = local;
    po.onUnauthorized = [this, raw] {
        // Una API key regenerada o una sesión revocada: se pide de nuevo la credencial, no se sigue con un proxy inútil. Se hace fuera del hilo del proxy.
        (void)QtConcurrent::run([this, raw] {
            QMutexLocker op(&g_opMutex);
            { QMutexLocker l(&m_mutex); if (m_active.get() != raw) return; }
            const QString id = raw->serverId, nm = raw->name, us = raw->user;
            if (!id.isEmpty()) m_servers.forgetSession(id);
            closeActive();
            setState(QJsonObject{{"tipo", "sesion-vencida"}, {"servidorId", id}, {"nombre", nm}, {"usuario", jstr(us)}});
        });
    };
    po.onFailure = [this, raw](const QString &reason) { onFailure(raw, reason); };
    a->proxy = std::make_unique<ClientProxy>(std::move(po));
    if (!a->proxy->start()) throw ClientError(QStringLiteral("sin-conexion"), QStringLiteral("No se pudo iniciar el puente local con el servidor."));
    const QJsonObject st{{"tipo", "conectada"}, {"info", a->info}, {"api", QJsonObject{{"baseUrl", a->proxy->baseUrl()}, {"token", a->proxy->token()}}}};
    {
        QMutexLocker l(&m_mutex);
        m_active = std::move(a);
    }
    setState(st);
    return st;
}

/**
 * El proxy no pudo hablar con el servidor. Un fallo suelto (una imagen, un tiempo de espera) no tira la sesión: se confirma con /health. Si de
 * verdad cayó se pasa a «sin conexión»; si el certificado cambió, a «certificado cambiado» (nunca se sigue hablando con un servidor que presenta
 * otra identidad).
 */
void ClientConnection::onFailure(Active *who, const QString &reason)
{
    {
        QMutexLocker l(&m_mutex);
        if (m_checkingFailure || m_active.get() != who) return;
        m_checkingFailure = true;
    }
    (void)QtConcurrent::run([this, who, reason] {
        struct Reset { ClientConnection *c; ~Reset() { QMutexLocker l(&c->m_mutex); c->m_checkingFailure = false; } } reset{this};
        QMutexLocker op(&g_opMutex);
        Remote::Dest dest;
        QString id, name;
        {
            QMutexLocker l(&m_mutex);
            if (m_active.get() != who) return;
            dest = who->dest; id = who->serverId; name = who->name;
        }
        const QString url = Remote::urlOf(dest);
        if (reason == QLatin1String("certificado-cambiado")) {
            const Remote::CertInfo c = Remote::readCertificate(dest.host, dest.port);
            const QString old = id.isEmpty() ? QString() : m_servers.get(id).fingerprint;
            closeActive();
            setState(QJsonObject{{"tipo", "certificado-cambiado"}, {"servidorId", id}, {"nombre", name}, {"url", url}, {"huella", old}, {"huellaNueva", c.ok ? QJsonValue(c.fingerprint) : QJsonValue(QJsonValue::Null)}});
            return;
        }
        const bool alive = Remote::requestJson(dest, QStringLiteral("/api/v1/health"), {}, nullptr, 3000).status == 200;
        { QMutexLocker l(&m_mutex); if (alive || m_active.get() != who) return; }
        closeActive();
        setState(QJsonObject{{"tipo", "sin-conexion"}, {"servidorId", id}, {"nombre", name}, {"url", url}});
    });
}

// ----------------------------------------------------------------- reconectar

QJsonObject ClientConnection::reconnect(const QString &serverId)
{
    QMutexLocker op(&g_opMutex);
    return wrap([&]() -> QJsonObject {
        const SavedServer s = m_servers.get(serverId);
        if (!s.valid()) throw ClientError(QStringLiteral("servidor-desconocido"), QStringLiteral("Este servidor ya no está guardado."));
        const QString name = s.name.isEmpty() ? s.host : s.name;
        Remote::Dest dest{s.host, s.port, s.https, s.trustCert ? s.certPem : QString()};
        const QString url = Remote::urlOf(dest);
        const bool isSession = s.credentialType == QLatin1String("sesion");
        auto expired = [&] {
            setState(QJsonObject{{"tipo", "sesion-vencida"}, {"servidorId", serverId}, {"nombre", name}, {"usuario", jstr(s.lastUser)}});
            return state();
        };
        if (isSession && (s.sessionToken.isEmpty() || s.sessionExpires <= QDateTime::currentSecsSinceEpoch())) return expired();
        const QString value = isSession ? s.sessionToken : s.apiKey;
        if (value.isEmpty()) return expired();

        const Remote::JsonResponse r = Remote::requestJson(dest, QStringLiteral("/api/v1/me"), {{"X-Api-Key", value.toUtf8()}}, nullptr, 6000);
        if (r.status == 0) {
            if (dest.https && r.certError) {
                // La huella que presenta AHORA, para que la persona la compare con la que ve el dueño en Ajustes → Servidor.
                const Remote::CertInfo c = Remote::readCertificate(dest.host, dest.port);
                setState(QJsonObject{{"tipo", "certificado-cambiado"}, {"servidorId", serverId}, {"nombre", name}, {"url", url}, {"huella", s.fingerprint}, {"huellaNueva", c.ok ? QJsonValue(c.fingerprint) : QJsonValue(QJsonValue::Null)}});
            } else setState(QJsonObject{{"tipo", "sin-conexion"}, {"servidorId", serverId}, {"nombre", name}, {"url", url}});
            return state();
        }
        if (r.status == 401) { m_servers.forgetSession(serverId); return expired(); }
        if (r.status != 200) {
            setState(QJsonObject{{"tipo", "sin-conexion"}, {"servidorId", serverId}, {"nombre", name}, {"url", url}});
            return state();
        }
        m_servers.save(s.host, s.port, s.https, {});
        return activate(serverId, name, dest, s.credentialType, s.lastUser, value, true);
    });
}

QJsonObject ClientConnection::renewSession(const QString &password)
{
    QString id;
    {
        const QJsonObject st = state();
        if (st.value(QStringLiteral("tipo")).toString() != QLatin1String("sesion-vencida"))
            return QJsonObject{{"ok", false}, {"codigo", "servidor-desconocido"}, {"mensaje", "No hay una sesión que renovar."}};
        id = st.value(QStringLiteral("servidorId")).toString();
    }
    QMutexLocker op(&g_opMutex);
    return wrap([&]() -> QJsonObject {
        const SavedServer s = m_servers.get(id);
        if (!s.valid() || s.lastUser.isEmpty()) throw ClientError(QStringLiteral("servidor-desconocido"), QStringLiteral("Este servidor ya no está guardado."));
        const Remote::Dest dest{s.host, s.port, s.https, s.trustCert ? s.certPem : QString()};
        const Cred cred = authenticate(dest, QJsonObject{{"tipo", "sesion"}, {"usuario", s.lastUser}, {"password", password}});
        m_servers.save(s.host, s.port, s.https, {{"sessionToken", cred.value}, {"sessionExpires", cred.expires}, {"lastUser", cred.user}});
        return activate(id, s.name.isEmpty() ? s.host : s.name, dest, QStringLiteral("sesion"), cred.user, cred.value, true);
    });
}

QJsonObject ClientConnection::trustNewCertificate(const QString &fingerprint)
{
    QString id;
    {
        const QJsonObject st = state();
        if (st.value(QStringLiteral("tipo")).toString() != QLatin1String("certificado-cambiado"))
            return QJsonObject{{"ok", false}, {"codigo", "servidor-desconocido"}, {"mensaje", "No hay un certificado pendiente de confirmar."}};
        id = st.value(QStringLiteral("servidorId")).toString();
    }
    {
        QMutexLocker op(&g_opMutex);
        const QJsonObject failed = [&]() -> QJsonObject {
            try {
                const SavedServer s = m_servers.get(id);
                if (!s.valid()) throw ClientError(QStringLiteral("servidor-desconocido"), QStringLiteral("Este servidor ya no está guardado."));
                const Remote::ProbeResult p = Remote::probe({s.host, s.port, s.https, QString()});
                if (p.type != QLatin1String("certificado-no-confiable") || !Remote::fingerprintMatches(fingerprint, p.fingerprint))
                    throw ClientError(QStringLiteral("certificado-cambiado"), QStringLiteral("La huella no coincide con el certificado que presenta el servidor."));
                m_servers.save(s.host, s.port, s.https, {{"trustCert", true}, {"fingerprint", p.fingerprint}, {"certPem", p.pem}});
                return {};
            } catch (const ClientError &e) {
                return QJsonObject{{"ok", false}, {"codigo", e.code}, {"mensaje", QString::fromUtf8(e.what())}};
            }
        }();
        if (!failed.isEmpty()) return failed;
    }
    return reconnect(id);
}

QJsonObject ClientConnection::retry()
{
    const QJsonObject st = state();
    const QString type = st.value(QStringLiteral("tipo")).toString();
    QString id = type == QLatin1String("conectada") ? st.value(QStringLiteral("info")).toObject().value(QStringLiteral("servidorId")).toString() : st.value(QStringLiteral("servidorId")).toString();
    if (type == QLatin1String("desconectada") || id.isEmpty()) return QJsonObject{{"ok", true}, {"estado", st}};
    return reconnect(id);
}

// ----------------------------------------------------------------- salir

void ClientConnection::disconnect()
{
    QMutexLocker op(&g_opMutex);
    closeActive();
    setState(QJsonObject{{"tipo", "desconectada"}});
}

/** «Olvidar este servidor»: borra lo recordado de él (token, certificado confiado, progreso local). */
void ClientConnection::forget()
{
    QMutexLocker op(&g_opMutex);
    const QJsonObject st = state();
    const QString type = st.value(QStringLiteral("tipo")).toString();
    const QString id = type == QLatin1String("conectada") ? st.value(QStringLiteral("info")).toObject().value(QStringLiteral("servidorId")).toString() : st.value(QStringLiteral("servidorId")).toString();
    if (id.isEmpty()) return;
    closeActive();
    m_servers.remove(id);
    setState(QJsonObject{{"tipo", "desconectada"}});
}

QJsonArray ClientConnection::remembered() const
{
    QJsonArray out;
    for (const SavedServer &s : m_servers.list())
        out.append(QJsonObject{{"id", s.id}, {"nombre", jstr(s.name)}, {"url", Remote::urlOf({s.host, s.port, s.https, QString()})}});
    return out;
}
