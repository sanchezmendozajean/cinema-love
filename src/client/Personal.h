#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrlQuery>
#include <functional>

#include "ClientState.h"

struct PersonalResponse {
    int status = 200;
    QJsonValue body;
};

/**
 * Lo personal del Modo Cliente con token: el servidor remoto pone el catálogo y ESTE equipo guarda el progreso, «Próximas a ver» y las
 * colecciones (specs/heredadas/03 §5). Responde en el mismo formato que las rutas de usuario del servidor para que la interfaz no note la
 * diferencia. BLOQUEANTE (consulta al servidor): se usa desde hilos de trabajo. Port de client/personal.ts.
 */
class Personal
{
public:
    /** Pide JSON al servidor remoto: GET si `body` es nulo, POST si no. Devuelve null si no pudo. */
    using Reader = std::function<QJsonValue(const QString &path, const QJsonValue *body)>;
    enum class Kind { None, Local, Query, Overlay };

    Personal(LocalState local, Reader remote) : m_local(std::move(local)), m_remote(std::move(remote)) {}

    /** ¿Esta ruta se atiende aquí (Local), se le pide al servidor con el progreso de este equipo (Query), se reenvía y se corrige después (Overlay) o va tal cual? */
    static Kind classify(const QString &method, const QString &path);

    PersonalResponse handle(const QString &method, const QString &path, const QUrlQuery &query, const QJsonValue &body);
    PersonalResponse query(const QString &path, const QUrlQuery &query);
    QJsonValue overlay(const QJsonValue &body);

private:
    QJsonValue withMyProgress(const QString &what, const QJsonObject &extra);
    QJsonArray elements(const QStringList &ids, const std::function<LocalProgress(const QString &)> &progressOf = {});
    QJsonArray collectionsJson(const QVector<LocalState::Collection> &cs);
    PersonalResponse queue(const QString &method, const QString &id, const QUrlQuery &query);
    PersonalResponse collections(const QString &method, const QStringList &seg, const QUrlQuery &query, const QJsonValue &body);

    LocalState m_local;
    Reader m_remote;
};
