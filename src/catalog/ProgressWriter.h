#pragma once
#include <QString>

namespace ProgressWriter {

/** ¿Es una copia de desarrollo? Regla de oro de los datos: solo se escribe en `.dev-data/`. */
bool isDevCopy(const QString &dbPath);

/**
 * Upsert en watch_progresses (usuario NULL), igual que escritura.ts de Electron: conserva las pistas si no llegan nuevas;
 * «visto» deja posición 0 y retira el ítem de proximas_a_ver. `position` en segundos enteros.
 */
bool save(const QString &dbPath, const QString &mediaItemId, qint64 position, const QString &state,
          const QString &audioId, const QString &subId);

} // namespace ProgressWriter
