#pragma once
#include <QString>
#include <QStringList>
#include <functional>
#include <stdexcept>

/**
 * Escáner de bibliotecas: port del LibraryScannerService de Flutter (specs/heredadas/06 §2–4). Mismas reglas de carpetas, mismo hash de
 * respaldo y misma reconciliación: sobre los mismos archivos produce las mismas `ruta_relativa`, `hash_respaldo`, temporadas y números
 * de episodio. Es BLOQUEANTE: se ejecuta en un hilo de trabajo (los trabajos de administración).
 */
namespace Scanner {

struct Library {
    QString id;
    QString type;     // pelicula | serie
    QString path;
};

struct Result {
    int nuevos = 0, reconciliados = 0, noEncontrados = 0, extrasNuevos = 0, extrasReconciliados = 0;
    int seriesNuevas = 0, seriesReconciliadas = 0, seriesNoEncontradas = 0;
    QStringList reconciliadosPorHashIds;   // movidos o renombrados: se muestran en la pantalla de reconciliación
};

struct Progress {
    QString phase;    // listando | procesando | cerrando
    int done = 0, total = 0;
    QString current;
};

struct Options {
    std::function<bool()> aborted;
    std::function<void(const Progress &)> onProgress;
};

struct Cancelled : std::runtime_error { Cancelled() : std::runtime_error("Escaneo cancelado") {} };
struct FolderUnavailable : std::runtime_error {
    explicit FolderUnavailable(const QString &p) : std::runtime_error(("La carpeta de la biblioteca no está disponible: " + p).toStdString()), path(p) {}
    QString path;
};

/** Hash liviano de respaldo: SHA-256 de los primeros 64 KiB ‖ últimos 64 KiB (si supera 64 KiB) ‖ UTF-8 del tamaño. */
QString backupHash(const QString &path);

bool isVideo(const QString &path);
bool samePath(const QString &a, const QString &b);

Result scanLibrary(const Library &lib, const Options &opts);

}
