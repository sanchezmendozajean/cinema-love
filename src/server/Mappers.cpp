#include "Mappers.h"

#include <QDateTime>
#include <QJsonDocument>

#include "Catalog.h"

namespace Mappers {

QString isoDate(qint64 secs) { return QDateTime::fromSecsSinceEpoch(secs, Qt::UTC).toString(Qt::ISODateWithMs); }

QJsonValue nz(const QVariant &v)   // NULL de SQL → null JSON
{
    if (!v.isValid() || v.isNull()) return QJsonValue::Null;
    switch (v.typeId()) {
    case QMetaType::Int: case QMetaType::LongLong: case QMetaType::UInt: case QMetaType::ULongLong: return v.toLongLong();
    case QMetaType::Double: return v.toDouble();
    default: return v.toString();
    }
}

QJsonArray genres(const QVariant &v)
{
    QJsonArray out;
    for (const QJsonValue &g : QJsonDocument::fromJson(v.toString().toUtf8()).array())
        if (g.isString()) out.append(g);
    return out;
}

QJsonArray people(const QVariant &v)
{
    QJsonArray out;
    for (const QJsonValue &pv : QJsonDocument::fromJson(v.toString().toUtf8()).array()) {
        const QJsonObject o = pv.toObject();
        if (!o.value(QStringLiteral("nombre")).isString()) continue;
        QJsonObject p;
        if (o.value(QStringLiteral("id")).isDouble()) p.insert(QStringLiteral("id"), o.value(QStringLiteral("id")));
        p.insert(QStringLiteral("nombre"), o.value(QStringLiteral("nombre")));
        const QString rol = o.value(QStringLiteral("rol")).isString() ? o.value(QStringLiteral("rol")).toString() : o.value(QStringLiteral("personaje")).toString();
        if (!rol.isEmpty()) p.insert(QStringLiteral("rol"), rol);
        if (!o.value(QStringLiteral("fotoUrl")).toString().isEmpty()) p.insert(QStringLiteral("fotoUrl"), o.value(QStringLiteral("fotoUrl")));
        out.append(p);
    }
    return out;
}

QJsonObject mediaItem(const QVariantMap &r, bool detail)
{
    QJsonObject o{{"id", r["id"].toString()}, {"libraryId", r["library_id"].toString()}, {"seriesId", nz(r["series_id"])},
                  {"numeroTemporada", nz(r["numero_temporada"])}, {"numeroEpisodio", nz(r["numero_episodio"])}, {"tmdbId", nz(r["tmdb_id"])},
                  {"titulo", nz(r["titulo"])}, {"anio", nz(r["anio"])}, {"posterUrl", nz(r["poster_url"])}, {"backdropUrl", nz(r["backdrop_url"])},
                  {"duracionSegundos", nz(r["duracion_segundos"])}, {"creadoEn", isoDate(r["creado_en"].toLongLong())}};
    if (!detail) { o.insert(QStringLiteral("generos"), genres(r["generos"])); return o; }
    o.insert(QStringLiteral("estadoArchivo"), r["estado_archivo"].toString());
    if (r["titulo"].isNull()) o.insert(QStringLiteral("tituloArchivo"), Catalog::fileName(r["ruta_relativa"].toString()));
    o.insert(QStringLiteral("tituloOriginal"), nz(r["titulo_original"]));
    o.insert(QStringLiteral("idiomaOriginal"), nz(r["idioma_original"]));
    o.insert(QStringLiteral("sinopsis"), nz(r["sinopsis"]));
    o.insert(QStringLiteral("estudio"), nz(r["estudio"]));
    o.insert(QStringLiteral("generos"), genres(r["generos"]));
    o.insert(QStringLiteral("reparto"), people(r["reparto"]));
    o.insert(QStringLiteral("equipo"), people(r["equipo"]));
    return o;
}

QJsonObject serie(const QVariantMap &r, bool detail)
{
    QJsonObject o{{"id", r["id"].toString()}, {"libraryId", r["library_id"].toString()}, {"tmdbId", nz(r["tmdb_id"])}, {"titulo", nz(r["titulo"])},
                  {"anio", nz(r["anio"])}, {"posterUrl", nz(r["poster_url"])}, {"backdropUrl", nz(r["backdrop_url"])}, {"creadoEn", isoDate(r["creado_en"].toLongLong())}};
    if (!detail) { o.insert(QStringLiteral("generos"), genres(r["generos"])); return o; }
    if (r["titulo"].isNull()) o.insert(QStringLiteral("tituloArchivo"), Catalog::fileName(r["ruta_relativa"].toString()));
    o.insert(QStringLiteral("tituloOriginal"), nz(r["titulo_original"]));
    o.insert(QStringLiteral("idiomaOriginal"), nz(r["idioma_original"]));
    o.insert(QStringLiteral("sinopsis"), nz(r["sinopsis"]));
    o.insert(QStringLiteral("estudio"), nz(r["estudio"]));
    o.insert(QStringLiteral("generos"), genres(r["generos"]));
    o.insert(QStringLiteral("reparto"), people(r["reparto"]));
    o.insert(QStringLiteral("equipo"), people(r["equipo"]));
    return o;
}


} // namespace Mappers
