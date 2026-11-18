#pragma once
#include <QMutex>
#include <QString>
#include <atomic>

/**
 * Espejo local del catálogo de un servidor remoto (Modo Cliente de la versión Qt). La interfaz lee el catálogo de un `cinemalove.sqlite`; para el
 * Modo Cliente se construye uno con el MISMO esquema a partir de la API del servidor (a través del proxy local, que ya lleva la credencial). Los
 * archivos no existen en este equipo: cada película apunta a su URL de streaming (`<proxy>/api/v1/media/<id>/stream`). Es una copia de lectura que
 * se reconstruye en cada conexión; lo que el usuario cambia (progreso, Próximas a ver, colecciones) se envía al servidor.
 *
 * Dos fases para que la interfaz aparezca pronto: (1) listados (títulos, pósters, progreso) y (2) fichas completas (sinopsis, reparto…).
 * BLOQUEANTE: se ejecuta en un hilo de trabajo.
 */
class Mirror
{
public:
    struct State {
        QString phase = QStringLiteral("inactivo");   // inactivo | listados | fichas | listo | error
        int done = 0, total = 0;
        QString path;                                  // .sqlite en cuanto la fase 1 terminó
        QString message;
    };

    Mirror(const QString &folder, const QString &proxyBaseUrl, const QString &proxyToken, const QString &serverId);

    void build();
    /** Vuelve a leer solo lo personal (progreso y «Próximas a ver»): rápido, tras una acción del usuario. */
    void updatePersonal();
    void cancel() { m_cancelled = true; }
    State state() const;

private:
    struct Impl;
    void setState(const State &s);

    QString m_folder, m_base, m_token, m_serverId;
    mutable QMutex m_mutex;
    State m_state;
    std::atomic<bool> m_cancelled{false};
};
