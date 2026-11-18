#include "Catalog.h"

#include <QDateTime>
#include <cmath>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <QSqlQuery>
#include <QSqlError>

#include "Store.h"

namespace Catalog {

namespace {

QJsonValue nz(const QVariant &v)
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

QVariant uidBind(const QString &userId) { return userId.isEmpty() ? QVariant(QMetaType::fromType<QString>()) : QVariant(userId); }

QString placeholders(int n) { return QStringList(QList<QString>(n, QStringLiteral("?"))).join(QLatin1Char(',')); }

QString label(const QVariant &t, const QVariant &e)
{
    return QStringLiteral("S%1E%2").arg(t.isNull() ? 0 : t.toInt(), 2, 10, QLatin1Char('0')).arg(e.isNull() ? 0 : e.toInt(), 2, 10, QLatin1Char('0'));
}

// ---------------------------------------------------------------- títulos normalizados (tabla TEMP)

void ensureNorm()
{
    static thread_local QString sig;
    const QString cur = Store::scalar(QStringLiteral(
        "SELECT (SELECT COUNT(*) || ':' || COALESCE(MAX(actualizado_en), 0) FROM media_items) || '/' || (SELECT COUNT(*) || ':' || COALESCE(MAX(actualizado_en), 0) FROM series)")).toString();
    if (cur == sig) return;
    QSqlDatabase db = Store::db();
    QSqlQuery q(db);
    q.exec(QStringLiteral("CREATE TEMP TABLE IF NOT EXISTS cl_norm (id TEXT PRIMARY KEY, tn TEXT, tno TEXT, nombre TEXT)"));
    db.transaction();
    q.exec(QStringLiteral("DELETE FROM cl_norm"));
    QSqlQuery ins(db);
    ins.prepare(QStringLiteral("INSERT OR REPLACE INTO cl_norm (id, tn, tno, nombre) VALUES (?, ?, ?, ?)"));
    for (const QString &sql : {QStringLiteral("SELECT id, titulo, titulo_original, ruta_relativa FROM media_items WHERE series_id IS NULL"),
                               QStringLiteral("SELECT id, titulo, titulo_original, ruta_relativa FROM series")}) {
        for (const QVariantMap &r : Store::rows(sql)) {
            const QString name = fileName(r["ruta_relativa"].toString());
            ins.addBindValue(r["id"]);
            ins.addBindValue(normalize(r["titulo"].isNull() ? name : r["titulo"].toString()));
            ins.addBindValue(r["titulo_original"].isNull() ? QString() : normalize(r["titulo_original"].toString()));
            ins.addBindValue(name);
            ins.exec();
        }
    }
    db.commit();
    sig = cur;
}

/** CTE «base»: películas (series_id nulo) + series visibles, con el progreso del principal. Replica baseCte del backend Node. */
QString baseCte(bool texto, bool ultima, const QString &userId, QVariantList &binds)
{
    const QVariant uid = uidBind(userId);
    QString s = QStringLiteral(
        "WITH base AS ("
        " SELECT 'pelicula' AS kind, m.id, m.library_id, m.titulo, m.titulo_original, %1 AS tn, %2 AS tno, m.anio, m.poster_url,"
        "  CASE WHEN m.generos IS NOT NULL AND json_valid(m.generos) THEN m.generos ELSE '[]' END AS generos,"
        "  m.duracion_segundos AS duracion, m.creado_en, wp.estado AS p_estado, wp.posicion_segundos AS p_pos, wp.ultima_reproduccion AS ultima,"
        "  CASE WHEN m.titulo IS NULL THEN n.nombre END AS titulo_archivo"
        " FROM media_items m LEFT JOIN cl_norm n ON n.id = m.id"
        " LEFT JOIN watch_progresses wp ON wp.media_item_id = m.id AND wp.usuario_id IS ?"
        " WHERE m.series_id IS NULL AND m.estado_archivo = 'ok'"
        " UNION ALL"
        " SELECT 'serie', s.id, s.library_id, s.titulo, s.titulo_original, %3, %4, s.anio, s.poster_url,"
        "  CASE WHEN s.generos IS NOT NULL AND json_valid(s.generos) THEN s.generos ELSE '[]' END,"
        "  NULL, s.creado_en, NULL, NULL, %5,"
        "  CASE WHEN s.titulo IS NULL THEN n2.nombre END"
        " FROM series s LEFT JOIN cl_norm n2 ON n2.id = s.id"
        " WHERE s.estado_archivo = 'ok')")
        .arg(texto ? QStringLiteral("n.tn") : QStringLiteral("NULL"), texto ? QStringLiteral("COALESCE(n.tno, '')") : QStringLiteral("NULL"),
             texto ? QStringLiteral("n2.tn") : QStringLiteral("NULL"), texto ? QStringLiteral("COALESCE(n2.tno, '')") : QStringLiteral("NULL"),
             ultima ? QStringLiteral("(SELECT MAX(wp.ultima_reproduccion) FROM media_items e JOIN watch_progresses wp ON wp.media_item_id = e.id AND wp.usuario_id IS ? WHERE e.series_id = s.id)")
                    : QStringLiteral("NULL"));
    binds << uid;
    if (ultima) binds << uid;
    return s;
}

QString escapeLike(const QString &s)
{
    QString o;
    for (const QChar c : s) {
        if (c == QLatin1Char('\\') || c == QLatin1Char('%') || c == QLatin1Char('_')) o += QLatin1Char('\\');
        o += c;
    }
    return o;
}

/** WHERE combinado con AND; `exclude` quita un filtro (patrón de facetas). */
QString buildWhere(const Filters &f, const QString &exclude, QVariantList &binds)
{
    QStringList parts;
    if (exclude != QLatin1String("q") && !f.q.isEmpty()) {
        for (const QString &t : normalize(f.q).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
            const QString like = QStringLiteral("%") + escapeLike(t) + QStringLiteral("%");
            parts << QStringLiteral("(b.tn LIKE ? ESCAPE '\\' OR b.tno LIKE ? ESCAPE '\\')");
            binds << like << like;
        }
    }
    if (exclude != QLatin1String("generos") && !f.generos.isEmpty()) {
        parts << QStringLiteral("EXISTS (SELECT 1 FROM json_each(b.generos) j WHERE j.value IN (%1))").arg(placeholders(f.generos.size()));
        for (const QString &g : f.generos) binds << g;
    }
    if (exclude != QLatin1String("bibliotecas") && !f.bibliotecas.isEmpty()) {
        parts << QStringLiteral("b.library_id IN (%1)").arg(placeholders(f.bibliotecas.size()));
        for (const QString &g : f.bibliotecas) binds << g;
    }
    if (exclude != QLatin1String("tipo") && !f.tipo.isEmpty()) { parts << QStringLiteral("b.kind = ?"); binds << f.tipo; }
    if (exclude != QLatin1String("anio")) {
        if (f.anioDesde) { parts << QStringLiteral("b.anio >= ?"); binds << *f.anioDesde; }
        if (f.anioHasta) { parts << QStringLiteral("b.anio <= ?"); binds << *f.anioHasta; }
    }
    if (exclude != QLatin1String("estado") && !f.estado.isEmpty()) {
        parts << QStringLiteral("b.kind = 'pelicula' AND COALESCE(b.p_estado, 'noVisto') = ?");
        binds << f.estado;
    }
    return parts.isEmpty() ? QString() : QStringLiteral("WHERE ") + parts.join(QStringLiteral(" AND "));
}

QString orderBy(const QString &orden)
{
    const QString o = orden.isEmpty() ? QStringLiteral("titulo") : orden;
    const bool desc = o.startsWith(QLatin1Char('-'));
    const QString k = desc ? o.mid(1) : o;
    QString key = QStringLiteral("COALESCE(b.tn, '')");
    if (k == QLatin1String("anio")) key = QStringLiteral("b.anio");
    else if (k == QLatin1String("agregado")) key = QStringLiteral("b.creado_en");
    else if (k == QLatin1String("reproducido")) key = QStringLiteral("b.ultima");
    // Los nulos siempre al final y el título desempata para que la paginación sea estable.
    return QStringLiteral("(%1 IS NULL), %1 %2, COALESCE(b.tn, ''), b.id").arg(key, desc ? QStringLiteral("DESC") : QStringLiteral("ASC"));
}

QJsonObject progressOf(const QVariant &estado, const QVariant &pos)
{
    return QJsonObject{{"estado", estado.toString()}, {"posicionSegundos", pos.isNull() ? 0 : pos.toLongLong()}};
}

struct SeriesStats { int temporadas = 0, episodios = 0, vistos = 0; };

QHash<QString, SeriesStats> seriesStats(const QStringList &ids, const QString &userId)
{
    QHash<QString, SeriesStats> out;
    if (ids.isEmpty()) return out;
    QVariantList b{uidBind(userId)};
    for (const QString &i : ids) b << i;
    for (const QVariantMap &r : Store::rows(QStringLiteral(
             "SELECT e.series_id AS id, COUNT(DISTINCT e.numero_temporada) AS temporadas, COUNT(*) AS episodios,"
             " SUM(CASE WHEN wp.estado = 'visto' THEN 1 ELSE 0 END) AS vistos"
             " FROM media_items e LEFT JOIN watch_progresses wp ON wp.media_item_id = e.id AND wp.usuario_id IS ?"
             " WHERE e.series_id IN (%1) AND e.estado_archivo = 'ok' GROUP BY e.series_id").arg(placeholders(ids.size())), b))
        out.insert(r["id"].toString(), {r["temporadas"].toInt(), r["episodios"].toInt(), r["vistos"].isNull() ? 0 : r["vistos"].toInt()});
    return out;
}

QJsonObject element(const QVariantMap &r, const QHash<QString, SeriesStats> &stats)
{
    QJsonObject o{{"kind", r["kind"].toString()}, {"id", r["id"].toString()}, {"libraryId", r["library_id"].toString()}, {"titulo", nz(r["titulo"])}};
    if (r["titulo"].isNull() && !r["titulo_archivo"].toString().isEmpty()) {
        o.insert(QStringLiteral("sinMetadatos"), true);
        o.insert(QStringLiteral("tituloArchivo"), r["titulo_archivo"].toString());
    }
    o.insert(QStringLiteral("anio"), nz(r["anio"]));
    o.insert(QStringLiteral("posterUrl"), nz(r["poster_url"]));
    o.insert(QStringLiteral("generos"), genres(r["generos"]));
    if (r["kind"].toString() == QLatin1String("pelicula")) {
        o.insert(QStringLiteral("duracionSegundos"), nz(r["duracion"]));
        o.insert(QStringLiteral("progreso"), r["p_estado"].isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(progressOf(r["p_estado"], r["p_pos"])));
    } else {
        const SeriesStats s = stats.value(r["id"].toString());
        o.insert(QStringLiteral("temporadas"), s.temporadas);
        o.insert(QStringLiteral("episodios"), s.episodios);
        o.insert(QStringLiteral("episodiosVistos"), s.vistos);
    }
    return o;
}

QJsonObject continueCard(const QVariantMap &r, const QJsonObject &progress)
{
    if (r["series_id"].isNull() || r["series_id"].toString().isEmpty()) {
        return QJsonObject{{"kind", "pelicula"}, {"id", r["id"].toString()}, {"titulo", nz(r["titulo"])}, {"posterUrl", nz(r["poster_url"])},
                           {"backdropUrl", nz(r["backdrop_url"])}, {"duracionSegundos", nz(r["duracion_segundos"])}, {"progreso", progress}};
    }
    const QVariant poster = r["poster_url"].isNull() ? r["s_poster"] : r["poster_url"];
    const QVariant backdrop = r["backdrop_url"].isNull() ? r["s_backdrop"] : r["backdrop_url"];
    return QJsonObject{{"kind", "episodio"}, {"id", r["id"].toString()}, {"titulo", nz(r["titulo"])}, {"posterUrl", nz(poster)}, {"backdropUrl", nz(backdrop)},
                       {"duracionSegundos", nz(r["duracion_segundos"])}, {"progreso", progress},
                       {"serie", QJsonObject{{"id", r["series_id"].toString()}, {"titulo", nz(r["s_titulo"])}, {"posterUrl", nz(r["s_poster"])}}},
                       {"episodio", label(r["numero_temporada"], r["numero_episodio"])}};
}

QJsonArray byIds(const QStringList &ids, const QString &userId, bool onlyMovies)
{
    if (ids.isEmpty()) return {};
    ensureNorm();
    QVariantList b;
    QString sql = baseCte(false, false, userId, b);
    sql += QStringLiteral(" SELECT b.* FROM base b WHERE ") + (onlyMovies ? QStringLiteral("b.kind = 'pelicula' AND ") : QString()) + QStringLiteral("b.id IN (%1)").arg(placeholders(ids.size()));
    for (const QString &i : ids) b << i;
    const QVector<QVariantMap> rows = Store::rows(sql, b);
    QStringList serieIds;
    for (const QVariantMap &r : rows) if (r["kind"].toString() == QLatin1String("serie")) serieIds << r["id"].toString();
    const auto stats = seriesStats(serieIds, userId);
    QHash<QString, QJsonObject> byId;
    for (const QVariantMap &r : rows) byId.insert(r["id"].toString(), element(r, stats));
    QJsonArray out;
    for (const QString &i : ids) if (byId.contains(i)) out.append(byId.value(i));
    return out;
}

QJsonArray continueWatching(const QString &userId, int limit = 20)
{
    QJsonArray out;
    for (const QVariantMap &r : Store::rows(QStringLiteral(
             "SELECT m.id, m.titulo, m.poster_url, m.backdrop_url, m.duracion_segundos, m.series_id, m.numero_temporada, m.numero_episodio,"
             " wp.posicion_segundos AS pos, wp.estado, s.titulo AS s_titulo, s.poster_url AS s_poster, s.backdrop_url AS s_backdrop"
             " FROM watch_progresses wp JOIN media_items m ON m.id = wp.media_item_id AND m.estado_archivo = 'ok'"
             " LEFT JOIN series s ON s.id = m.series_id"
             " WHERE wp.usuario_id IS ? AND wp.estado = 'enProgreso' ORDER BY wp.ultima_reproduccion DESC, wp.id DESC LIMIT ?"),
             {uidBind(userId), limit}))
        out.append(continueCard(r, QJsonObject{{"estado", "enProgreso"}, {"posicionSegundos", r["pos"].toLongLong()}}));
    return out;
}

QJsonArray featured(const QString &userId, const QJsonArray &cont, int max = 5)
{
    QJsonArray out;
    QSet<QString> used;
    auto add = [&](const QJsonObject &d) {
        const QString id = d["id"].toString();
        const QString sid = d["serie"].isObject() ? d["serie"].toObject()["id"].toString() : QString();
        if (used.contains(id) || (!sid.isEmpty() && used.contains(sid))) return;
        used.insert(id);
        if (!sid.isEmpty()) used.insert(sid);
        out.append(d);
    };
    for (const QJsonValue &cv : cont) {
        const QJsonObject c = cv.toObject();
        if (out.size() >= 3 || c["backdropUrl"].isNull() || c["backdropUrl"].toString().isEmpty()) continue;
        const QVariantMap r = Store::row(QStringLiteral("SELECT anio, sinopsis, generos FROM media_items WHERE id = ?"), {c["id"].toString()});
        if (r.isEmpty()) continue;
        const bool ep = c["serie"].isObject();
        QJsonObject d{{"kind", c["kind"]}, {"id", c["id"]}, {"titulo", ep ? c["serie"].toObject()["titulo"] : c["titulo"]}, {"anio", nz(r["anio"])},
                      {"sinopsis", nz(r["sinopsis"])}, {"generos", genres(r["generos"])}, {"posterUrl", c["posterUrl"]}, {"backdropUrl", c["backdropUrl"]},
                      {"duracionSegundos", c["duracionSegundos"]}, {"progreso", c["progreso"]}};
        if (ep) {
            d.insert(QStringLiteral("serie"), QJsonObject{{"id", c["serie"].toObject()["id"]}, {"titulo", c["serie"].toObject()["titulo"]}});
            d.insert(QStringLiteral("episodio"), c["episodio"]);
        }
        add(d);
    }
    for (const QVariantMap &r : Store::rows(QStringLiteral(
             "SELECT m.id, m.titulo, m.anio, m.sinopsis, m.generos, m.poster_url, m.backdrop_url, m.duracion_segundos, wp.estado, wp.posicion_segundos AS pos"
             " FROM media_items m LEFT JOIN watch_progresses wp ON wp.media_item_id = m.id AND wp.usuario_id IS ?"
             " WHERE m.series_id IS NULL AND m.estado_archivo = 'ok' AND m.backdrop_url IS NOT NULL AND COALESCE(wp.estado, 'noVisto') = 'noVisto'"
             " ORDER BY m.creado_en DESC, m.id LIMIT ?"), {uidBind(userId), max * 2})) {
        if (out.size() >= max) break;
        add(QJsonObject{{"kind", "pelicula"}, {"id", r["id"].toString()}, {"titulo", nz(r["titulo"])}, {"anio", nz(r["anio"])}, {"sinopsis", nz(r["sinopsis"])},
                        {"generos", genres(r["generos"])}, {"posterUrl", nz(r["poster_url"])}, {"backdropUrl", nz(r["backdrop_url"])},
                        {"duracionSegundos", nz(r["duracion_segundos"])},
                        {"progreso", r["estado"].isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(progressOf(r["estado"], r["pos"]))}});
    }
    return out;
}

} // namespace

QString normalize(const QString &s)
{
    const QString d = s.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(d.size());
    for (const QChar c : d) {
        const QChar::Category cat = c.category();
        if (cat != QChar::Mark_NonSpacing && cat != QChar::Mark_SpacingCombining && cat != QChar::Mark_Enclosing) out += c;
    }
    return out.toLower().trimmed();
}

QString fileName(const QString &rel)
{
    const QStringList parts = rel.split(QRegularExpression(QStringLiteral("[\\\\/]")), Qt::SkipEmptyParts);
    QString last = parts.isEmpty() ? QString() : parts.last();
    last.remove(QRegularExpression(QStringLiteral("\\.(mp4|mkv|avi|mov|m4v|wmv|flv|webm|ts|mpg|mpeg)$"), QRegularExpression::CaseInsensitiveOption));
    last.replace(QRegularExpression(QStringLiteral("[._]+")), QStringLiteral(" "));
    last.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return last.trimmed();
}

QString parseFilters(const QHash<QString, QString> &query, Filters *out)
{
    auto list = [&](const QString &k) {
        QStringList l;
        for (const QString &p : query.value(k).split(QLatin1Char(','))) if (!p.trimmed().isEmpty()) l << p.trimmed();
        return l;
    };
    auto integer = [&](const QString &k) -> std::optional<int> {
        const QString v = query.value(k).trimmed();
        if (v.isEmpty()) return std::nullopt;
        bool ok = false;
        const double d = v.toDouble(&ok);
        if (!ok || !std::isfinite(d)) return std::nullopt;
        return int(d);
    };
    Filters f;
    if (!query.value(QStringLiteral("q")).trimmed().isEmpty()) f.q = query.value(QStringLiteral("q"));
    f.generos = list(QStringLiteral("generos"));
    f.bibliotecas = list(QStringLiteral("bibliotecas"));
    if (query.contains(QStringLiteral("tipo"))) {
        const QString t = query.value(QStringLiteral("tipo"));
        if (t != QLatin1String("pelicula") && t != QLatin1String("serie")) return QStringLiteral("tipo inválido (pelicula | serie).");
        f.tipo = t;
    }
    f.anioDesde = integer(QStringLiteral("anioDesde"));
    f.anioHasta = integer(QStringLiteral("anioHasta"));
    if (query.contains(QStringLiteral("estado"))) {
        const QString e = query.value(QStringLiteral("estado"));
        if (e != QLatin1String("noVisto") && e != QLatin1String("enProgreso") && e != QLatin1String("visto")) return QStringLiteral("estado inválido.");
        f.estado = e;
    }
    if (query.contains(QStringLiteral("orden"))) {
        QString o = query.value(QStringLiteral("orden"));
        const QString k = o.startsWith(QLatin1Char('-')) ? o.mid(1) : o;
        if (k != QLatin1String("titulo") && k != QLatin1String("anio") && k != QLatin1String("agregado") && k != QLatin1String("reproducido")) return QStringLiteral("orden inválido.");
        f.orden = o;
    }
    f.pagina = integer(QStringLiteral("pagina"));
    f.tamano = integer(QStringLiteral("tamano"));
    *out = f;
    return {};
}

QJsonObject search(const Filters &f, const QString &userId)
{
    ensureNorm();
    const int tamano = qMin(qMax(f.tamano.value_or(60), 1), 200);
    const int pagina = qMax(f.pagina.value_or(1), 1);
    QString ordenPor = f.orden.isEmpty() ? QStringLiteral("titulo") : f.orden;
    if (ordenPor.startsWith(QLatin1Char('-'))) ordenPor.remove(0, 1);

    // total
    QVariantList cb;
    QString countSql = baseCte(!f.q.isEmpty(), false, userId, cb);
    countSql += QStringLiteral(" SELECT COUNT(*) AS n FROM base b ");
    countSql += buildWhere(f, QString(), cb);
    const qint64 total = Store::scalar(countSql, cb).toLongLong();

    // página
    QVariantList pb;
    QString pageSql = baseCte(!f.q.isEmpty() || ordenPor == QLatin1String("titulo"), ordenPor == QLatin1String("reproducido"), userId, pb);
    pageSql += QStringLiteral(" SELECT b.* FROM base b ");
    pageSql += buildWhere(f, QString(), pb);
    pageSql += QStringLiteral(" ORDER BY ") + orderBy(f.orden) + QStringLiteral(" LIMIT ? OFFSET ?");
    pb << tamano << (pagina - 1) * tamano;
    const QVector<QVariantMap> rows = Store::rows(pageSql, pb);
    QStringList serieIds;
    for (const QVariantMap &r : rows) if (r["kind"].toString() == QLatin1String("serie")) serieIds << r["id"].toString();
    const auto stats = seriesStats(serieIds, userId);
    QJsonArray items;
    for (const QVariantMap &r : rows) items.append(element(r, stats));

    // facetas: cada una sobre el resultado filtrado SIN su propio filtro. (Los textos se componen en sentencias separadas: el orden de
    // evaluación de los operandos de «+» no está definido y los parámetros deben quedar en el orden del SQL.)
    const bool facetTexto = !f.q.isEmpty();
    QJsonArray gen;
    {
        QVariantList b;
        QString sql = baseCte(facetTexto, false, userId, b);
        sql += QStringLiteral(" SELECT j.value AS valor, COUNT(*) AS conteo FROM base b, json_each(b.generos) j ");
        sql += buildWhere(f, QStringLiteral("generos"), b);
        sql += QStringLiteral(" GROUP BY j.value ORDER BY conteo DESC, valor COLLATE NOCASE");
        for (const QVariantMap &r : Store::rows(sql, b)) gen.append(QJsonObject{{"valor", r["valor"].toString()}, {"conteo", r["conteo"].toLongLong()}});
    }
    QJsonArray libs;
    {
        QVariantList b;
        QString sql = baseCte(facetTexto, false, userId, b);
        sql += QStringLiteral(" SELECT b.library_id AS id, COUNT(*) AS conteo FROM base b ");
        sql += buildWhere(f, QStringLiteral("bibliotecas"), b);
        sql += QStringLiteral(" GROUP BY b.library_id");
        QHash<QString, qint64> counts;
        for (const QVariantMap &r : Store::rows(sql, b)) counts.insert(r["id"].toString(), r["conteo"].toLongLong());
        for (const QVariantMap &l : Store::rows(QStringLiteral("SELECT id, nombre, tipo FROM libraries ORDER BY nombre COLLATE NOCASE")))
            libs.append(QJsonObject{{"id", l["id"].toString()}, {"nombre", l["nombre"].toString()}, {"tipo", l["tipo"].toString()}, {"conteo", counts.value(l["id"].toString(), 0)}});
    }
    QJsonObject years;
    {
        QVariantList b;
        QString sql = baseCte(facetTexto, false, userId, b);
        sql += QStringLiteral(" SELECT MIN(b.anio) AS min, MAX(b.anio) AS max FROM base b ");
        sql += buildWhere(f, QStringLiteral("anio"), b);
        const QVariantMap r = Store::row(sql, b);
        years = QJsonObject{{"min", nz(r["min"])}, {"max", nz(r["max"])}};
    }
    return QJsonObject{{"total", total}, {"pagina", pagina}, {"tamano", tamano}, {"elementos", items},
                       {"facetas", QJsonObject{{"generos", gen}, {"bibliotecas", libs}, {"anios", years}}}};
}

QJsonArray elementsByIds(const QStringList &ids, const QString &userId, bool onlyMovies) { return byIds(ids, userId, onlyMovies); }

QJsonValue withExternalStates(const QVector<ExternalState> &states, const std::function<QJsonValue()> &fn)
{
    QSqlDatabase db = Store::db();
    QSqlQuery q(db);
    q.exec(QStringLiteral("CREATE TEMP TABLE watch_progresses (id INTEGER PRIMARY KEY AUTOINCREMENT, media_item_id TEXT NOT NULL, usuario_id TEXT NULL,"
                          " posicion_segundos INTEGER NOT NULL DEFAULT 0, estado TEXT NOT NULL, ultima_reproduccion INTEGER NULL)"));
    struct Drop { QSqlDatabase d; ~Drop() { QSqlQuery x(d); x.exec(QStringLiteral("DROP TABLE temp.watch_progresses")); } } drop{db};
    QVector<ExternalState> sorted = states;
    // De la más antigua a la más reciente: el `id` desempata el orden de «continuar viendo».
    std::stable_sort(sorted.begin(), sorted.end(), [](const ExternalState &a, const ExternalState &b) { return a.last.toLongLong() < b.last.toLongLong(); });
    db.transaction();
    QSqlQuery ins(db);
    ins.prepare(QStringLiteral("INSERT INTO temp.watch_progresses (media_item_id, posicion_segundos, estado, ultima_reproduccion) VALUES (?, ?, ?, ?)"));
    for (const ExternalState &e : std::as_const(sorted)) {
        ins.addBindValue(e.id);
        ins.addBindValue(e.position);
        ins.addBindValue(e.state);
        ins.addBindValue(e.last.isNull() ? QVariant(QMetaType::fromType<qint64>()) : e.last);
        ins.exec();
    }
    db.commit();
    return fn();
}

QJsonArray continueSummaries(const QStringList &ids)
{
    if (ids.isEmpty()) return {};
    QVariantList b;
    for (const QString &i : ids) b << i;
    QHash<QString, QJsonObject> byId;
    for (const QVariantMap &r : Store::rows(QStringLiteral(
             "SELECT m.id, m.titulo, m.poster_url, m.backdrop_url, m.duracion_segundos, m.series_id, m.numero_temporada, m.numero_episodio, 0 AS pos,"
             " s.titulo AS s_titulo, s.poster_url AS s_poster, s.backdrop_url AS s_backdrop"
             " FROM media_items m LEFT JOIN series s ON s.id = m.series_id WHERE m.estado_archivo = 'ok' AND m.id IN (%1)").arg(placeholders(ids.size())), b))
        byId.insert(r["id"].toString(), continueCard(r, QJsonObject{{"estado", "noVisto"}, {"posicionSegundos", 0}}));
    QJsonArray out;
    for (const QString &i : ids) if (byId.contains(i)) out.append(byId.value(i));
    return out;
}

QJsonObject home(const QString &userId)
{
    const QJsonArray cont = continueWatching(userId);
    const QJsonArray feat = featured(userId, cont);
    QStringList queueIds, seenIds;
    for (const QVariantMap &r : Store::rows(QStringLiteral("SELECT p.media_item_id AS id FROM proximas_a_ver p WHERE p.usuario_id IS ? ORDER BY p.agregado_en DESC, p.id DESC LIMIT ?"), {uidBind(userId), 30}))
        queueIds << r["id"].toString();
    for (const QVariantMap &r : Store::rows(QStringLiteral("SELECT wp.media_item_id AS id FROM watch_progresses wp WHERE wp.usuario_id IS ? AND wp.estado = 'visto' ORDER BY wp.ultima_reproduccion DESC, wp.id DESC LIMIT ?"), {uidBind(userId), 60}))
        seenIds << r["id"].toString();
    QJsonArray seen = byIds(seenIds, userId, true), seenCut;
    for (int i = 0; i < qMin(20, int(seen.size())); ++i) seenCut.append(seen.at(i));
    Filters f;
    f.orden = QStringLiteral("-agregado");
    f.tamano = 20;
    return QJsonObject{{"continuarViendo", cont}, {"proximasAVer", byIds(queueIds, userId, true)},
                       {"agregadosRecientemente", search(f, userId)["elementos"].toArray()}, {"vistosRecientemente", seenCut},
                       {"destacados", feat}, {"destacado", feat.isEmpty() ? QJsonValue(QJsonValue::Null) : feat.at(0)}};
}

} // namespace Catalog
