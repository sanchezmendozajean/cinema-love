#pragma once
#include <QString>
#include <functional>
#include <memory>
#include <optional>

#include "ClientState.h"
#include "Remote.h"

class HttpServer;

/**
 * Listener local (127.0.0.1) del Modo Cliente: la interfaz sigue hablando con un solo origen local —igual que en Modo Local— y este proxy
 * reenvía `/api/v1/*` al servidor remoto con SU credencial (que nunca sale hacia la interfaz). Con token, lo personal se atiende en este equipo.
 * Port de client/proxy-cliente.ts.
 */
class ClientProxy
{
public:
    struct Options {
        Remote::Dest dest;
        std::function<QString()> credential;                 // credencial del servidor remoto (token de sesión o API key)
        QString serverName;
        std::optional<LocalState> personal;                  // con token: progreso/listas/colecciones locales
        std::function<void()> onUnauthorized;                // el servidor respondió 401 a una credencial que antes servía
        std::function<void(const QString &)> onFailure;      // «sin-conexion» | «certificado-cambiado»
    };

    explicit ClientProxy(Options o);
    ~ClientProxy();
    /** Escucha en un puerto libre; false si no pudo. */
    bool start();
    void stop();
    QString baseUrl() const { return m_baseUrl; }
    QString token() const { return m_token; }

private:
    Options m_o;
    std::unique_ptr<HttpServer> m_http;
    QString m_baseUrl, m_token;
};
