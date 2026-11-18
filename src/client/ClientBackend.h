#pragma once
#include <QString>
#include <memory>

class HttpServer;
class ClientConnection;
class ClientDb;
class Mirror;
class Mdns;

/**
 * Backend del Modo Cliente, integrado en la app: `cliente_datos.sqlite`, la conexión con el otro CinemaLove, el proxy local y el
 * espejo del catálogo. La interfaz lo gestiona por un pequeño servidor de control en 127.0.0.1 (token efímero): /control/estado|probar|conectar|
 * reconectar|renovar|confiar|reintentar|refrescar|personal|desconectar|olvidar|descubrir
 * (servidores CinemaLove anunciados por mDNS en la red). Una vez conectado, el resto de la API va al proxy local.
 */
class ClientBackend
{
public:
    ClientBackend();
    ~ClientBackend();
    /** `folder` guarda cliente_datos.sqlite y el espejo. Devuelve el error o vacío. */
    QString start(const QString &folder);
    void stop();
    QString controlUrl() const { return m_url; }
    QString controlToken() const { return m_token; }

private:
    void launchMirror();
    void stopMirror();

    QString m_folder, m_url, m_token;
    std::unique_ptr<ClientDb> m_db;
    std::unique_ptr<ClientConnection> m_conn;
    std::unique_ptr<HttpServer> m_http;
    std::shared_ptr<Mirror> m_mirror;
    std::unique_ptr<Mdns> m_mdns;
};
