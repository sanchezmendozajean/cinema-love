#pragma once
#include <QJsonObject>
#include <QString>
#include <memory>

#include "Mdns.h"
#include <stdexcept>

class HttpServer;

/**
 * Servidor público («Compartir mi colección en la red», specs/heredadas/03 §4): un segundo listener HTTP en 0.0.0.0 con la misma API pero
 * SIN administración. Se inicia y detiene sin reiniciar la app. Protocolo y puerto no se cambian con el servidor corriendo.
 */
class PublicServer
{
public:
    struct Error : std::runtime_error {
        Error(const QString &m, int s = 400) : std::runtime_error(m.toStdString()), status(s) {}
        int status;
    };

    PublicServer();
    ~PublicServer();

    bool active() const { return m_http != nullptr; }
    QJsonObject config() const;
    QJsonObject state() const;
    QJsonObject view() const { return QJsonObject{{"config", config()}, {"estado", state()}}; }
    void saveConfig(const QJsonObject &c);
    void start();
    void stop();
    void regenerateCertificate();

    /** Nombre amigable del servidor (el configurado o el del equipo). */
    static QString serverName();

private:
    void updateAnnouncement();

    std::unique_ptr<HttpServer> m_http;
    Mdns m_mdns;
    QString m_runProtocol;
    int m_runPort = 0;
};

/** Rutas de administración del servidor público, sesiones activas y QR. */
void registerServerAdminRoutes(HttpServer &h, PublicServer &ps);
