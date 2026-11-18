#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>

/**
 * Consultas del catálogo unificado y de Inicio (puerto de repositories/catalogo.ts del backend Node). `userId` vacío = admin local
 * (`usuario_id IS NULL`). SQLite de Qt no permite registrar funciones SQL, así que los títulos normalizados (sin acentos, en minúsculas)
 * y el nombre derivado del archivo viven en una tabla TEMP (`temp.cl_norm`) que se recalcula cuando cambia la colección.
 */
namespace Catalog {

struct Filters {
    QString q;
    QStringList generos;
    QStringList bibliotecas;
    QString tipo;                 // pelicula | serie
    std::optional<int> anioDesde, anioHasta;
    QString estado;               // noVisto | enProgreso | visto
    QString orden;                // titulo | anio | agregado | reproducido (prefijo - = descendente)
    std::optional<int> pagina, tamano;
};

/** Valida los parámetros de /catalog; devuelve el mensaje de error o vacío. */
QString parseFilters(const QHash<QString, QString> &query, Filters *out);

/** Nombre legible derivado de una ruta relativa (sin extensión, con puntos y guiones bajos como espacios). */
QString fileName(const QString &rel);

/** Normaliza texto: sin acentos, minúsculas y sin espacios en los extremos. */
QString normalize(const QString &s);

QJsonObject search(const Filters &f, const QString &userId);
QJsonObject home(const QString &userId);
QJsonArray elementsByIds(const QStringList &ids, const QString &userId, bool onlyMovies);
QJsonArray continueSummaries(const QStringList &ids);

} // namespace Catalog
