#pragma once
#include <QString>

/** bcrypt ($2a$/$2b$/$2y$), compatible con los hashes que ya guardan las bases de CinemaLove (bcryptjs). */
namespace Bcrypt {
/** Comprueba una contraseña contra un hash bcrypt. */
bool verify(const QString &password, const QString &hash);
/** Genera un hash `$2b$<cost>$…` con sal aleatoria. */
QString hash(const QString &password, int cost = 10);
}
