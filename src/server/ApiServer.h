#pragma once
#include <QString>
#include <memory>

class HttpServer;
class PublicServer;

/**
 * API HTTP v1 del backend integrado (specs/heredadas/04): el contrato compatible con los clientes Flutter, dentro de la aplicación.
 * Escucha solo en 127.0.0.1 con un token efímero de administración (listener interno).
 */
class ApiServer
{
public:
    ApiServer();
    ~ApiServer();
    /** Arranca sobre la colección ya abierta en `Store`. Devuelve el puerto (0 si falló). */
    quint16 start(quint16 port = 0);
    void stop();
    QString adminToken() const { return m_adminToken; }
    QString baseUrl() const;
    /** Instala las rutas de la API en `h`. Con `adminToken` vacío es el listener público (sin administración). */
    static void installRoutes(HttpServer &h, const QString &adminToken, PublicServer *ps);

private:
    std::unique_ptr<HttpServer> m_http;
    std::unique_ptr<PublicServer> m_public;
    QString m_adminToken;
    quint16 m_port = 0;
};
