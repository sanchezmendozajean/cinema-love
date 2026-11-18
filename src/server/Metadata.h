#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <functional>
#include <stdexcept>

/**
 * Cliente de TMDb y asistente de metadatos (port de scanner/tmdb-client.ts, tmdb-match.ts y metadata.ts). Todo es BLOQUEANTE
 * (usa un bucle de eventos local por petición): se ejecuta en hilos de trabajo, nunca en el hilo del servidor HTTP.
 */
namespace Metadata {

/** Error de TMDb con un mensaje listo para mostrar. `fatal`: no tiene sentido seguir (clave inválida, sin red). */
struct TmdbError : std::runtime_error {
    TmdbError(const QString &m, bool fatal_ = true, int status_ = 0) : std::runtime_error(m.toStdString()), fatal(fatal_), status(status_) {}
    bool fatal;
    int status;
};

/** Resultados de búsqueda: [{id, titulo, tituloOriginal, anio, posterUrl, sinopsis}]. `tipo`: pelicula | serie. */
QJsonArray search(const QString &apiKey, const QString &type, const QString &query, int year /* 0 = sin año */);
void testKey(const QString &apiKey);

/** Asigna a mano el resultado elegido (película/episodio suelto o serie completa con sus temporadas). */
void assignManual(const QString &kind /* item | serie */, const QString &id, int tmdbId, const QString &apiKey, const std::function<void(const QString &)> &line = {});

/** Edición manual de campos de metadata (los ausentes no se tocan). `table`: media_items | series. */
void editMetadata(const QString &table, const QString &id, const QJsonObject &edit);

struct ImportEvent {
    bool isLine = false;
    QString text;
    int processed = 0, total = 0, resolved = 0;
};

struct ImportOptions {
    bool onlyMissing = true;
    std::function<bool()> aborted;
    std::function<void(const ImportEvent &)> onEvent;
};

/** Asistente de metadatos de una biblioteca: solo asigna con una coincidencia inequívoca; devuelve el resumen JSON. */
QJsonObject importLibrary(const QString &libId, const QString &libType, const QString &apiKey, const ImportOptions &opts);

}
