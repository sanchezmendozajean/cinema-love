#pragma once
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <functional>
#include <memory>
#include <stdexcept>

#include "ClientProxy.h"
#include "ClientState.h"

/** Error con un código que la interfaz traduce a su mensaje/pantalla. `huella`/`serverName` acompañan a los de certificado. */
struct ClientError : std::runtime_error {
    ClientError(const QString &code_, const QString &m, const QString &fp = {}, const QString &name = {}) : std::runtime_error(m.toStdString()), code(code_), fingerprint(fp), serverName(name) {}
    QString code, fingerprint, serverName;
};

/**
 * Conexión de este equipo a otro CinemaLove (Modo Cliente): verifica el servidor, se autentica (usuario o token), fija el certificado
 * autofirmado por huella, recuerda el servidor y reconecta. La contraseña nunca se guarda: en modo persistente se guarda el token de sesión;
 * la API key sí. Port de client/conexion-cliente.ts. Las operaciones son BLOQUEANTES (red): se ejecutan en hilos de trabajo.
 */
class ClientConnection
{
public:
    explicit ClientConnection(const ClientDb &db);
    ~ClientConnection();

    /** Estado: {tipo: desconectada|conectada|sin-conexion|sesion-vencida|certificado-cambiado, …} (formato de specs/cliente-types). */
    QJsonObject state() const;
    void setListener(std::function<void(const QJsonObject &)> f) { m_listener = std::move(f); }

    /** Paso «Verificar»: {tipo: ok|certificado-no-confiable|certificado-cambiado|sin-respuesta|no-cinemalove|url-invalida, …}. */
    QJsonObject probe(const QString &url);
    /** {ok: true, estado} o {ok: false, codigo, mensaje, huella?, nombreServidor?}. Petición: {url, credencial:{tipo,…}, recordar, confiarHuella?}. */
    QJsonObject connectTo(const QJsonObject &request);
    QJsonObject reconnect(const QString &serverId);
    QJsonObject renewSession(const QString &password);
    QJsonObject trustNewCertificate(const QString &fingerprint);
    QJsonObject retry();
    void disconnect();
    void forget();
    QJsonArray remembered() const;

private:
    struct Active { QJsonObject info; Remote::Dest dest; std::unique_ptr<ClientProxy> proxy; QString serverId, name, user; };

    QJsonObject activate(const QString &serverId, const QString &name, const Remote::Dest &dest, const QString &credential, const QString &user, const QString &value, bool remembered);
    void closeActive();
    void setState(const QJsonObject &s);
    void onFailure(Active *who, const QString &reason);
    QJsonObject wrap(const std::function<QJsonObject()> &fn);

    const ClientDb &m_db;
    Servers m_servers;
    mutable QMutex m_mutex;
    QJsonObject m_state{{"tipo", "desconectada"}};
    std::unique_ptr<Active> m_active;
    std::unique_ptr<ClientDb> m_sessionDb;          // estado local de una conexión que no se recuerda (solo dura la sesión)
    QString m_sessionDbPath;
    std::function<void(const QJsonObject &)> m_listener;
    bool m_checkingFailure = false;
};
