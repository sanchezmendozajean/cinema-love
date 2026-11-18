#pragma once
#include <QJsonArray>
#include <QString>

struct HttpRequest;

/**
 * Reproducciones «directas» recientes (panel «Reproduciendo ahora»). El streaming de un archivo con Range no abre sesión: se considera
 * activo mientras el cliente siga pidiendo bytes (ventana de 20 s). Compartido entre los servidores de todos los hilos.
 */
namespace Activity {
/** Marca actividad de la petición `r` sobre el ítem (`item`) o extra (`extra`) `id`. */
void mark(const HttpRequest &r, const QString &kind, const QString &id);
/** Lista de actividad vigente, por antigüedad: [{quien, titulo, cliente, desde, ultimaVez}]. */
QJsonArray active(int windowMs = 20000);
}
