#include "Metadata.h"

#include <QDateTime>
#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QThread>
#include <QUrlQuery>
#include <QUuid>

#include "Naming.h"
#include "Store.h"

namespace Metadata {

namespace {

const QString kBase = QStringLiteral("https://api.themoviedb.org/3");
const QString kPoster = QStringLiteral("https://image.tmdb.org/t/p/w500");
const QString kBackdrop = QStringLiteral("https://image.tmdb.org/t/p/w1280");
const QString kProfile = QStringLiteral("https://image.tmdb.org/t/p/w185");
const QString kStill = QStringLiteral("https://image.tmdb.org/t/p/w300");
// es-MX y no es-ES: TMDb guarda traducciones por país. Lo que no tenga traducción en es-MX se completa CAMPO POR CAMPO con en-US.
const QString kMainLang = QStringLiteral("es-MX");
const QString kFallbackLang = QStringLiteral("en-US");

QVariant nullVar() { return QVariant(QMetaType::fromType<QString>()); }
QVariant strOrNull(const QJsonValue &v) { return v.isString() ? QVariant(v.toString()) : nullVar(); }

QNetworkAccessManager &nam()
{
    static thread_local QNetworkAccessManager *n = nullptr;
    if (!n) {
        n = new QNetworkAccessManager();
        n->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    }
    return *n;
}

/** GET bloqueante a la API v3 con reintentos ante 429 (espera creciente o Retry-After). */
QJsonObject get(const QString &route, const QList<QPair<QString, QString>> &params)
{
    QUrl url(kBase + QLatin1Char('/') + route);
    QUrlQuery q;
    for (const auto &p : params) q.addQueryItem(p.first, p.second);
    url.setQuery(q);
    for (int attempt = 0;; ++attempt) {
        QNetworkRequest req(url);
        req.setTransferTimeout(15000);
        QNetworkReply *reply = nam().get(req);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QByteArray retryAfter = reply->rawHeader("Retry-After");
        reply->deleteLater();
        if (status == 0) throw TmdbError(QStringLiteral("No se pudo conectar con TMDb. Revisa tu conexión a internet."));
        if (status == 429 && attempt < 4) {
            const double asked = retryAfter.toDouble();
            QThread::msleep(asked > 0 ? quint32(asked * 1000) : quint32(1000 << attempt));
            continue;
        }
        if (status == 401) throw TmdbError(QStringLiteral("La API key de TMDb no es válida."), true, 401);
        if (status == 429) throw TmdbError(QStringLiteral("TMDb está limitando las peticiones. Inténtalo de nuevo en unos minutos."), true, 429);
        if (status != 200) throw TmdbError(QStringLiteral("TMDb respondió con un error (%1).").arg(status), status >= 500, status);
        return QJsonDocument::fromJson(body).object();
    }
}

/** Pide con es-MX y, si algún campo de texto viene vacío, completa solo esos campos con en-US. */
QJsonObject withFallback(const QString &route, QList<QPair<QString, QString>> params, const QStringList &fields)
{
    QList<QPair<QString, QString>> main = params;
    main.append({QStringLiteral("language"), kMainLang});
    QJsonObject principal = get(route, main);
    QStringList missing;
    for (const QString &f : fields) if (principal.value(f).toString().trimmed().isEmpty()) missing << f;
    if (missing.isEmpty()) return principal;
    try {
        QList<QPair<QString, QString>> fb = params;
        fb.append({QStringLiteral("language"), kFallbackLang});
        const QJsonObject backup = get(route, fb);
        for (const QString &f : missing) principal.insert(f, backup.value(f));
    } catch (const TmdbError &) {
        // sin red en el 2.º intento: mejor un campo vacío que romper toda la importación
    }
    return principal;
}

int yearOf(const QJsonValue &v)
{
    if (!v.isString() || v.toString().size() < 4) return 0;
    bool ok = false;
    const int n = v.toString().left(4).toInt(&ok);
    return ok ? n : 0;
}

QVariant urlOf(const QString &base, const QJsonValue &path) { return path.isString() && !path.toString().isEmpty() ? QVariant(base + path.toString()) : nullVar(); }

QJsonValue jv(const QVariant &v) { return v.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue::fromVariant(v); }

struct Person { QVariant id; QString name; QVariant role, photo; };

QList<Person> cast(const QJsonObject &credits)
{
    QList<Person> out;
    for (const QJsonValue &cv : credits.value(QStringLiteral("cast")).toArray()) {
        const QJsonObject c = cv.toObject();
        const QString ch = c.value(QStringLiteral("character")).toString();
        out.append({c.value(QStringLiteral("id")).isDouble() ? QVariant(c.value(QStringLiteral("id")).toInt()) : nullVar(), c.value(QStringLiteral("name")).toString(),
                    ch.isEmpty() ? nullVar() : QVariant(ch), urlOf(kProfile, c.value(QStringLiteral("profile_path")))});
    }
    return out;
}

QList<Person> crew(const QJsonObject &credits)
{
    static const QHash<QString, QString> roles = {{"Director", "Director"}, {"Screenplay", "Guionista"}, {"Writer", "Guionista"}, {"Story", "Guionista"},
                                                  {"Producer", "Productor"}, {"Executive Producer", "Productor ejecutivo"}};
    QList<Person> out;
    QSet<int> seen;
    for (const QJsonValue &cv : credits.value(QStringLiteral("crew")).toArray()) {
        const QJsonObject c = cv.toObject();
        const QString label = roles.value(c.value(QStringLiteral("job")).toString());
        if (label.isEmpty() || !c.value(QStringLiteral("id")).isDouble()) continue;
        const int id = c.value(QStringLiteral("id")).toInt();
        if (seen.contains(id)) continue;
        seen.insert(id);
        out.append({id, c.value(QStringLiteral("name")).toString(), label, urlOf(kProfile, c.value(QStringLiteral("profile_path")))});
    }
    return out;
}

/** Reparto/equipo como los guarda Flutter: lista JSON (`[]` si está vacía) y sin las claves nulas. */
QString peopleJson(const QList<Person> &list)
{
    QJsonArray arr;
    for (const Person &p : list) {
        QJsonObject o;
        if (!p.id.isNull()) o.insert(QStringLiteral("id"), p.id.toInt());
        o.insert(QStringLiteral("nombre"), p.name);
        if (!p.role.isNull()) o.insert(QStringLiteral("rol"), p.role.toString());
        if (!p.photo.isNull()) o.insert(QStringLiteral("fotoUrl"), p.photo.toString());
        arr.append(o);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

struct Detail {
    int id = 0;
    QString title;
    QVariant originalTitle, originalLang, year, overview, studio, poster, backdrop, duration;
    QStringList genres;
    QList<Person> cast, crew;
};

Detail getDetail(const QString &apiKey, bool movie, int id)
{
    const QJsonObject r = withFallback(movie ? QStringLiteral("movie/%1").arg(id) : QStringLiteral("tv/%1").arg(id),
                                       {{QStringLiteral("api_key"), apiKey}, {QStringLiteral("append_to_response"), QStringLiteral("credits")}},
                                       {movie ? QStringLiteral("title") : QStringLiteral("name"), QStringLiteral("overview")});
    const QJsonObject credits = r.value(QStringLiteral("credits")).toObject();
    Detail d;
    d.id = r.value(QStringLiteral("id")).toInt();
    d.title = r.value(movie ? QStringLiteral("title") : QStringLiteral("name")).toString();
    d.originalTitle = strOrNull(r.value(movie ? QStringLiteral("original_title") : QStringLiteral("original_name")));
    d.originalLang = strOrNull(r.value(QStringLiteral("original_language")));
    const int y = yearOf(r.value(movie ? QStringLiteral("release_date") : QStringLiteral("first_air_date")));
    d.year = y ? QVariant(y) : nullVar();
    d.overview = strOrNull(r.value(QStringLiteral("overview")));
    QStringList companies;
    for (const QJsonValue &c : r.value(QStringLiteral("production_companies")).toArray())
        if (c.toObject().value(QStringLiteral("name")).isString() && !c.toObject().value(QStringLiteral("name")).toString().isEmpty()) companies << c.toObject().value(QStringLiteral("name")).toString();
    d.studio = companies.isEmpty() ? nullVar() : QVariant(companies.first());
    d.poster = urlOf(kPoster, r.value(QStringLiteral("poster_path")));
    d.backdrop = urlOf(kBackdrop, r.value(QStringLiteral("backdrop_path")));
    d.duration = nullVar();
    if (movie) {
        const int min = r.value(QStringLiteral("runtime")).toInt();
        if (min > 0) d.duration = min * 60;
    } else {
        const QJsonArray eps = r.value(QStringLiteral("episode_run_time")).toArray();
        if (!eps.isEmpty()) d.duration = eps.first().toInt() * 60;
    }
    for (const QJsonValue &g : r.value(QStringLiteral("genres")).toArray()) d.genres << g.toObject().value(QStringLiteral("name")).toString();
    d.cast = cast(credits);
    d.crew = crew(credits);
    return d;
}

struct Episode { QVariant tmdbId; int number; QVariant name, overview, still, duration; };
struct Season { QVariant tmdbId, name, overview, poster; QList<Episode> episodes; QList<Person> cast, crew; };

Season getSeason(const QString &apiKey, int seriesTmdbId, int number)
{
    const QJsonObject r = withFallback(QStringLiteral("tv/%1/season/%2").arg(seriesTmdbId).arg(number),
                                       {{QStringLiteral("api_key"), apiKey}, {QStringLiteral("append_to_response"), QStringLiteral("credits")}},
                                       {QStringLiteral("name"), QStringLiteral("overview")});
    const QJsonObject credits = r.value(QStringLiteral("credits")).toObject();
    Season s;
    s.tmdbId = r.value(QStringLiteral("id")).isDouble() ? QVariant(r.value(QStringLiteral("id")).toInt()) : nullVar();
    s.name = strOrNull(r.value(QStringLiteral("name")));
    s.overview = strOrNull(r.value(QStringLiteral("overview")));
    s.poster = urlOf(kPoster, r.value(QStringLiteral("poster_path")));
    for (const QJsonValue &ev : r.value(QStringLiteral("episodes")).toArray()) {
        const QJsonObject e = ev.toObject();
        const int min = e.value(QStringLiteral("runtime")).toInt();
        s.episodes.append({e.value(QStringLiteral("id")).isDouble() ? QVariant(e.value(QStringLiteral("id")).toInt()) : nullVar(), e.value(QStringLiteral("episode_number")).toInt(),
                           strOrNull(e.value(QStringLiteral("name"))), strOrNull(e.value(QStringLiteral("overview"))), urlOf(kStill, e.value(QStringLiteral("still_path"))),
                           min > 0 ? QVariant(min * 60) : nullVar()});
    }
    s.cast = cast(credits);
    s.crew = crew(credits);
    return s;
}

qint64 now() { return QDateTime::currentSecsSinceEpoch(); }

void saveItem(const QString &id, const Detail &d)
{
    Store::exec(QStringLiteral("UPDATE media_items SET tmdb_id = ?, titulo = ?, titulo_original = ?, idioma_original = ?, anio = ?, sinopsis = ?, estudio = ?, poster_url = ?, backdrop_url = ?, duracion_segundos = ?, generos = ?, reparto = ?, equipo = ?, actualizado_en = ? WHERE id = ?"),
                {QString::number(d.id), d.title, d.originalTitle, d.originalLang, d.year, d.overview, d.studio, d.poster, d.backdrop, d.duration,
                 QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(d.genres)).toJson(QJsonDocument::Compact)), peopleJson(d.cast), peopleJson(d.crew), now(), id});
}

void saveSeries(const QString &id, const Detail &d)
{
    Store::exec(QStringLiteral("UPDATE series SET tmdb_id = ?, titulo = ?, titulo_original = ?, idioma_original = ?, anio = ?, sinopsis = ?, estudio = ?, poster_url = ?, backdrop_url = ?, generos = ?, reparto = ?, equipo = ?, actualizado_en = ? WHERE id = ?"),
                {QString::number(d.id), d.title, d.originalTitle, d.originalLang, d.year, d.overview, d.studio, d.poster, d.backdrop,
                 QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(d.genres)).toJson(QJsonDocument::Compact)), peopleJson(d.cast), peopleJson(d.crew), now(), id});
}

/** Guarda la temporada (upsert por (serie, número)) y completa cada episodio cuyo número case con el de TMDb. El still va en poster_url. */
void saveSeason(const QString &seriesId, int number, const Season &t)
{
    const qint64 ts = now();
    Store::begin();
    const QVariantMap ex = Store::row(QStringLiteral("SELECT id FROM seasons WHERE series_id = ? AND numero_temporada = ?"), {seriesId, number});
    const QVariant tid = t.tmdbId.isNull() ? nullVar() : QVariant(QString::number(t.tmdbId.toInt()));
    if (!ex.isEmpty())
        Store::exec(QStringLiteral("UPDATE seasons SET tmdb_id = ?, titulo = ?, sinopsis = ?, poster_url = ?, reparto = ?, equipo = ?, actualizado_en = ? WHERE id = ?"),
                    {tid, t.name, t.overview, t.poster, peopleJson(t.cast), peopleJson(t.crew), ts, ex["id"]});
    else
        Store::exec(QStringLiteral("INSERT INTO seasons (id, series_id, numero_temporada, tmdb_id, titulo, sinopsis, poster_url, reparto, equipo, creado_en, actualizado_en) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"),
                    {QUuid::createUuid().toString(QUuid::WithoutBraces), seriesId, number, tid, t.name, t.overview, t.poster, peopleJson(t.cast), peopleJson(t.crew), ts, ts});
    const QVector<QVariantMap> eps = Store::rows(QStringLiteral("SELECT id, numero_episodio FROM media_items WHERE series_id = ? AND numero_temporada = ? AND estado_archivo = 'ok'"), {seriesId, number});
    for (const Episode &e : t.episodes)
        for (const QVariantMap &ep : eps)
            if (ep["numero_episodio"].toInt() == e.number && !ep["numero_episodio"].isNull()) {
                Store::exec(QStringLiteral("UPDATE media_items SET tmdb_id = ?, titulo = ?, sinopsis = ?, poster_url = ?, duracion_segundos = ?, actualizado_en = ? WHERE id = ?"),
                            {e.tmdbId.isNull() ? nullVar() : QVariant(QString::number(e.tmdbId.toInt())), e.name, e.overview, e.still, e.duration, ts, ep["id"]});
                break;
            }
    Store::commit();
}

void importSeasons(const QString &seriesId, int tmdbSeriesId, const QString &apiKey, const std::function<void(const QString &)> &line, const std::function<bool()> &aborted)
{
    QList<int> numbers;
    for (const QVariantMap &r : Store::rows(QStringLiteral("SELECT DISTINCT numero_temporada n FROM media_items WHERE series_id = ? AND numero_temporada IS NOT NULL ORDER BY n"), {seriesId})) numbers << r["n"].toInt();
    for (const int n : numbers) {
        if (aborted && aborted()) return;
        line(QStringLiteral("  Descargando temporada %1...").arg(n));
        try {
            const Season t = getSeason(apiKey, tmdbSeriesId, n);
            saveSeason(seriesId, n, t);
            line(QStringLiteral("  Temporada %1: %2 episodio(s). ✓").arg(n).arg(t.episodes.size()));
        } catch (const TmdbError &e) {
            if (e.status == 401) throw;
            line(QStringLiteral("  Temporada %1: error (%2), se omite.").arg(n).arg(QString::fromUtf8(e.what())));
        }
    }
}

// ---------------------------------------------------------------- coincidencia

QString normalizeMatch(const QString &s)
{
    QString t = s.toLower().trimmed();
    t.remove(QRegularExpression(QStringLiteral("[^a-z0-9 ]")));
    t.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return t.trimmed();
}

/** Coincidencia inequívoca entre los resultados de TMDb y el nombre parseado; -1 = revisión manual. */
int pickBest(const QJsonArray &results, const Naming::ParsedTitle &parsed)
{
    if (results.isEmpty()) return -1;
    const QString wanted = normalizeMatch(parsed.title);
    auto matches = [&](const QJsonObject &r) {
        return normalizeMatch(r.value(QStringLiteral("titulo")).toString()) == wanted
               || (r.value(QStringLiteral("tituloOriginal")).isString() && normalizeMatch(r.value(QStringLiteral("tituloOriginal")).toString()) == wanted);
    };
    QList<int> idx;
    if (parsed.year) {
        for (int i = 0; i < results.size(); ++i) {
            const QJsonValue y = results.at(i).toObject().value(QStringLiteral("anio"));
            if (y.isDouble() && y.toInt() == *parsed.year) idx << i;
        }
        if (idx.size() == 1) return idx.first();
        if (idx.isEmpty()) return -1;
        QList<int> exact;
        for (const int i : idx) if (matches(results.at(i).toObject())) exact << i;
        return exact.size() == 1 ? exact.first() : -1;
    }
    for (int i = 0; i < results.size(); ++i) if (matches(results.at(i).toObject())) idx << i;
    return idx.size() == 1 ? idx.first() : -1;
}

} // namespace

QJsonArray search(const QString &apiKey, const QString &type, const QString &query, int year)
{
    if (query.trimmed().isEmpty()) return {};
    const bool movie = type == QLatin1String("pelicula");
    QList<QPair<QString, QString>> params{{QStringLiteral("api_key"), apiKey}, {QStringLiteral("query"), query}, {QStringLiteral("language"), kMainLang}, {QStringLiteral("include_adult"), QStringLiteral("false")}};
    if (year > 0) params.append({movie ? QStringLiteral("year") : QStringLiteral("first_air_date_year"), QString::number(year)});
    const QJsonObject r = get(movie ? QStringLiteral("search/movie") : QStringLiteral("search/tv"), params);
    QJsonArray out;
    for (const QJsonValue &xv : r.value(QStringLiteral("results")).toArray()) {
        const QJsonObject x = xv.toObject();
        const int y = yearOf(x.value(movie ? QStringLiteral("release_date") : QStringLiteral("first_air_date")));
        out.append(QJsonObject{{"id", x.value(QStringLiteral("id"))}, {"titulo", x.value(movie ? QStringLiteral("title") : QStringLiteral("name")).toString()},
                               {"tituloOriginal", x.value(movie ? QStringLiteral("original_title") : QStringLiteral("original_name")).isString() ? x.value(movie ? QStringLiteral("original_title") : QStringLiteral("original_name")) : QJsonValue(QJsonValue::Null)},
                               {"anio", y ? QJsonValue(y) : QJsonValue(QJsonValue::Null)}, {"posterUrl", jv(urlOf(kPoster, x.value(QStringLiteral("poster_path"))))},
                               {"sinopsis", x.value(QStringLiteral("overview")).isString() ? x.value(QStringLiteral("overview")) : QJsonValue(QJsonValue::Null)}});
    }
    return out;
}

void testKey(const QString &apiKey) { get(QStringLiteral("configuration"), {{QStringLiteral("api_key"), apiKey}}); }

void assignManual(const QString &kind, const QString &id, int tmdbId, const QString &apiKey, const std::function<void(const QString &)> &line)
{
    auto say = [&](const QString &t) { if (line) line(t); };
    if (kind == QLatin1String("serie")) {
        say(QStringLiteral("Descargando ficha de la serie..."));
        saveSeries(id, getDetail(apiKey, false, tmdbId));
        importSeasons(id, tmdbId, apiKey, say, {});
        return;
    }
    say(QStringLiteral("Descargando metadatos..."));
    saveItem(id, getDetail(apiKey, true, tmdbId));
}

void editMetadata(const QString &table, const QString &id, const QJsonObject &e)
{
    static const QList<QPair<QString, QString>> cols = {{"titulo", "titulo"}, {"tituloOriginal", "titulo_original"}, {"idiomaOriginal", "idioma_original"}, {"anio", "anio"},
                                                        {"sinopsis", "sinopsis"}, {"estudio", "estudio"}, {"posterUrl", "poster_url"}, {"backdropUrl", "backdrop_url"},
                                                        {"duracionSegundos", "duracion_segundos"}, {"generos", "generos"}, {"reparto", "reparto"}, {"equipo", "equipo"}};
    QStringList sets;
    QVariantList vals;
    for (const auto &c : cols) {
        if (!e.contains(c.first)) continue;
        if (table == QLatin1String("series") && c.first == QLatin1String("duracionSegundos")) continue;
        const QJsonValue v = e.value(c.first);
        sets << c.second + QStringLiteral(" = ?");
        if (c.first == QLatin1String("generos")) vals << QString::fromUtf8(QJsonDocument(v.isArray() ? v.toArray() : QJsonArray()).toJson(QJsonDocument::Compact));
        else if (c.first == QLatin1String("reparto") || c.first == QLatin1String("equipo")) {
            QList<Person> list;
            for (const QJsonValue &pv : v.toArray()) {
                const QJsonObject p = pv.toObject();
                list.append({p.value(QStringLiteral("id")).isDouble() ? QVariant(p.value(QStringLiteral("id")).toInt()) : nullVar(), p.value(QStringLiteral("nombre")).toString(),
                             p.value(QStringLiteral("rol")).isString() ? QVariant(p.value(QStringLiteral("rol")).toString()) : nullVar(),
                             p.value(QStringLiteral("fotoUrl")).isString() ? QVariant(p.value(QStringLiteral("fotoUrl")).toString()) : nullVar()});
            }
            vals << peopleJson(list);
        } else if (v.isString() && v.toString().trimmed().isEmpty()) vals << nullVar();
        else if (v.isNull() || v.isUndefined()) vals << nullVar();
        else vals << v.toVariant();
    }
    if (sets.isEmpty()) return;
    vals << now() << id;
    Store::exec(QStringLiteral("UPDATE %1 SET %2, actualizado_en = ? WHERE id = ?").arg(table, sets.join(QStringLiteral(", "))), vals);
}

QJsonObject importLibrary(const QString &libId, const QString &libType, const QString &apiKey, const ImportOptions &o)
{
    auto emitLine = [&](const QString &t) { if (o.onEvent) { ImportEvent e; e.isLine = true; e.text = t; o.onEvent(e); } };
    auto emitProgress = [&](int p, int t, int r) { if (o.onEvent) { ImportEvent e; e.processed = p; e.total = t; e.resolved = r; o.onEvent(e); } };
    const bool isSeries = libType == QLatin1String("serie");
    const QString table = isSeries ? QStringLiteral("series") : QStringLiteral("media_items");
    const QString sql = QStringLiteral("SELECT id, ruta_relativa FROM %1 WHERE library_id = ? AND estado_archivo = 'ok'%2%3 ORDER BY ruta_relativa")
                            .arg(table, isSeries ? QString() : QStringLiteral(" AND series_id IS NULL"), o.onlyMissing ? QStringLiteral(" AND tmdb_id IS NULL") : QString());
    const QVector<QVariantMap> pending = Store::rows(sql, {libId});
    QJsonArray ambiguous, none;
    int resolved = 0;
    bool cancelled = false;
    auto summary = [&] { return QJsonObject{{"total", int(pending.size())}, {"resueltos", resolved}, {"ambiguos", ambiguous}, {"sinCoincidencia", none}, {"cancelado", cancelled}}; };
    if (pending.isEmpty()) {
        emitLine(isSeries ? QStringLiteral("No hay series pendientes de metadata.") : QStringLiteral("No hay ítems pendientes de metadata."));
        return summary();
    }
    for (int i = 0; i < pending.size(); ++i) {
        if (o.aborted && o.aborted()) {
            emitLine(QStringLiteral("— Cancelado por el usuario. —"));
            cancelled = true;
            return summary();
        }
        const QVariantMap &item = pending.at(i);
        const Naming::ParsedTitle parsed = Naming::parseTitle(item["ruta_relativa"].toString());
        QJsonObject pend{{"id", item["id"].toString()}, {"ruta", item["ruta_relativa"].toString()}, {"titulo", parsed.title}, {"tipo", isSeries ? "serie" : "item"}};
        if (parsed.year) pend.insert(QStringLiteral("anio"), *parsed.year);
        emitLine(parsed.title + (parsed.year ? QStringLiteral(" (%1)").arg(*parsed.year) : QString()));
        emitLine(QStringLiteral("  Ubicación: ") + item["ruta_relativa"].toString());
        try {
            emitLine(QStringLiteral("  Buscando en TMDb..."));
            const QJsonArray results = search(apiKey, libType, parsed.title, parsed.year.value_or(0));
            const int best = pickBest(results, parsed);
            if (best < 0) {
                (results.isEmpty() ? none : ambiguous).append(pend);
                emitLine(results.isEmpty() ? QStringLiteral("  Sin resultados en TMDb — queda para revisión manual.") : QStringLiteral("  Varios candidatos, ninguno inequívoco — queda para revisión manual."));
            } else {
                const QJsonObject chosen = results.at(best).toObject();
                emitLine(QStringLiteral("  Encontrado: \"%1\" (%2)").arg(chosen.value(QStringLiteral("titulo")).toString(), chosen.value(QStringLiteral("anio")).isDouble() ? QString::number(chosen.value(QStringLiteral("anio")).toInt()) : QStringLiteral("—")));
                emitLine(isSeries ? QStringLiteral("  Descargando ficha de la serie...") : QStringLiteral("  Descargando metadatos..."));
                const int tmdbId = chosen.value(QStringLiteral("id")).toInt();
                const Detail d = getDetail(apiKey, !isSeries, tmdbId);
                if (isSeries) {
                    saveSeries(item["id"].toString(), d);
                    emitLine(QStringLiteral("  Ficha de la serie guardada. ✓"));
                    importSeasons(item["id"].toString(), tmdbId, apiKey, emitLine, o.aborted);
                } else {
                    saveItem(item["id"].toString(), d);
                    emitLine(QStringLiteral("  Guardado. ✓"));
                }
                resolved++;
            }
        } catch (const TmdbError &e) {
            if (e.fatal) {
                emitLine(QStringLiteral("  Error: ") + QString::fromUtf8(e.what()));
                emitProgress(i + 1, int(pending.size()), resolved);
                throw;   // problema de red o de API key: no tiene sentido seguir intentando
            }
            emitLine(QStringLiteral("  Error: %1 — se omite este %2.").arg(QString::fromUtf8(e.what()), isSeries ? QStringLiteral("serie") : QStringLiteral("ítem")));
        }
        emitProgress(i + 1, int(pending.size()), resolved);
    }
    return summary();
}

} // namespace Metadata
