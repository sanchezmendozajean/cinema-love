#include <QDebug>
#include "Mirror.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent>

#include "Remote.h"

namespace {

constexpr int kSchemaVersion = 11;

QVariant nullVar() { return QVariant(QMetaType::fromType<QString>()); }
QVariant txt(const QJsonValue &v) { return v.isString() && !v.toString().isEmpty() ? QVariant(v.toString()) : nullVar(); }
QVariant num(const QJsonValue &v) { return v.isDouble() ? QVariant(qint64(v.toDouble())) : nullVar(); }
QString genresJson(const QJsonValue &v) { return QString::fromUtf8(QJsonDocument(v.isArray() ? v.toArray() : QJsonArray()).toJson(QJsonDocument::Compact)); }
qint64 secs(const QJsonValue &iso)
{
    const QDateTime d = QDateTime::fromString(iso.toString(), Qt::ISODate);
    return d.isValid() ? d.toSecsSinceEpoch() : QDateTime::currentSecsSinceEpoch();
}

struct Conn {
    QString name;
    QSqlDatabase db;
    ~Conn() { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(name); }
};

/** Cliente de la API del proxy local (HTTP plano, sin TLS). */
struct Api {
    Remote::Dest dest;
    QString token;
    QJsonValue get(const QString &path) const
    {
        const Remote::JsonResponse r = Remote::requestJson(dest, QStringLiteral("/api/v1") + path, {{"X-Api-Key", token.toUtf8()}}, nullptr, 30000);
        return r.status == 200 ? r.json : QJsonValue(QJsonValue::Null);
    }
};

bool exec(QSqlQuery &q, const QVariantList &binds)
{
    for (const QVariant &b : binds) q.addBindValue(b);
    const bool ok = q.exec();
    q.finish();
    if (!ok) qWarning().noquote() << "[espejo] SQL:" << q.lastError().text();
    return ok;
}

}

Mirror::Mirror(const QString &folder, const QString &proxyBaseUrl, const QString &proxyToken, const QString &serverId)
    : m_folder(folder), m_base(proxyBaseUrl), m_token(proxyToken), m_serverId(serverId.isEmpty() ? QStringLiteral("servidor") : serverId) {}

Mirror::State Mirror::state() const { QMutexLocker l(&m_mutex); return m_state; }
void Mirror::setState(const State &s) { QMutexLocker l(&m_mutex); m_state = s; }

namespace {

/** Progreso y «Próximas a ver» del usuario en el servidor, copiados al espejo (solo para mostrar; el servidor es la fuente). */
void copyPersonal(QSqlDatabase db, const Api &api)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QSet<QString> existing;
    {
        QSqlQuery q(db);
        q.exec(QStringLiteral("SELECT id FROM media_items"));
        while (q.next()) existing.insert(q.value(0).toString());
    }
    // Orden de «continuar viendo» según /home
    QHash<QString, qint64> order;
    const QJsonValue home = api.get(QStringLiteral("/home"));
    int i = 0;
    for (const QJsonValue &c : home.toObject().value(QStringLiteral("continuarViendo")).toArray()) order.insert(c.toObject().value(QStringLiteral("id")).toString(), now - (i++) * 60);

    QHash<QString, QString> states;
    QStringList libs, series;
    { QSqlQuery q(db); q.exec(QStringLiteral("SELECT id FROM libraries")); while (q.next()) libs << q.value(0).toString(); }
    { QSqlQuery q(db); q.exec(QStringLiteral("SELECT id FROM series")); while (q.next()) series << q.value(0).toString(); }
    for (const QString &l : libs) { const QJsonObject o = api.get(QStringLiteral("/libraries/%1/progress-states").arg(l)).toObject(); for (auto it = o.begin(); it != o.end(); ++it) states.insert(it.key(), it.value().toString()); }
    for (const QString &s : series) {
        const QJsonObject o = api.get(QStringLiteral("/series/%1/progress-states").arg(s)).toObject();
        for (auto it = o.begin(); it != o.end(); ++it) states.insert(it.key(), it.value().isObject() ? it.value().toObject().value(QStringLiteral("estado")).toString() : it.value().toString());
    }
    db.transaction();
    QSqlQuery ins(db);
    ins.prepare(QStringLiteral("INSERT INTO watch_progresses (media_item_id, usuario_id, posicion_segundos, estado, ultima_reproduccion, pista_audio_id, pista_subtitulo_id) VALUES (?, NULL, ?, ?, ?, ?, ?)"));
    for (auto it = states.begin(); it != states.end(); ++it) {
        if (!existing.contains(it.key()) || (it.value() != QLatin1String("visto") && it.value() != QLatin1String("enProgreso"))) continue;
        qint64 pos = 0;
        QVariant audio = nullVar(), sub = nullVar();
        if (it.value() == QLatin1String("enProgreso")) {
            const QJsonObject p = api.get(QStringLiteral("/media/%1/progress").arg(it.key())).toObject();
            pos = qint64(p.value(QStringLiteral("posicionSegundos")).toDouble());
            audio = txt(p.value(QStringLiteral("pistaAudioId")));
            sub = txt(p.value(QStringLiteral("pistaSubtituloId")));
        }
        exec(ins, {it.key(), pos, it.value(), order.value(it.key(), now - 86400 * 30), audio, sub});
    }
    QSqlQuery insW(db);
    insW.prepare(QStringLiteral("INSERT INTO proximas_a_ver (media_item_id, usuario_id, agregado_en) VALUES (?, NULL, ?)"));
    int k = 0;
    for (const QJsonValue &m : api.get(QStringLiteral("/watchlist")).toArray()) {
        const QString id = m.toObject().value(QStringLiteral("id")).toString();
        if (existing.contains(id)) exec(insW, {id, now - (k++) * 60});
    }
    db.commit();
}

}

void Mirror::build()
{
    m_cancelled = false;
    const QString dir = m_folder + QStringLiteral("/espejo");
    QDir().mkpath(dir);
    const QString path = QDir::toNativeSeparators(dir + QLatin1Char('/') + m_serverId + QStringLiteral(".sqlite"));
    for (const QString &ext : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm"), QStringLiteral("-journal")}) QFile::remove(path + ext);
    Conn c;
    c.name = QStringLiteral("cl-espejo-%1").arg(quintptr(QThread::currentThreadId()));
    c.db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), c.name);
    c.db.setDatabaseName(path);
    c.db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    State st;
    if (!c.db.open()) { st.phase = QStringLiteral("error"); st.message = c.db.lastError().text(); setState(st); return; }
    QSqlQuery q(c.db);
    q.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    q.finish();
    {
        QFile schema(QStringLiteral(":/server/schema-v11.sql"));
        schema.open(QIODevice::ReadOnly);
        c.db.transaction();
        for (const QString &s : QString::fromUtf8(schema.readAll()).split(QLatin1Char(';'), Qt::SkipEmptyParts)) { QStringList keep; for (const QString &ln : s.split(QLatin1Char('\n'))) if (!ln.trimmed().startsWith(QLatin1String("--"))) keep << ln; const QString t = keep.join(QLatin1Char('\n')).trimmed(); if (!t.isEmpty()) q.exec(t); }
        q.exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion));
        c.db.commit();
    }
    Remote::Dest dest;
    dest.host = QStringLiteral("127.0.0.1");
    dest.port = QUrl(m_base).port();
    const Api api{dest, m_token};

    st.phase = QStringLiteral("listados");
    st.message = QStringLiteral("Leyendo el catálogo del servidor…");
    setState(st);

    const QJsonValue libsV = api.get(QStringLiteral("/libraries"));
    if (!libsV.isArray()) { st.phase = QStringLiteral("error"); st.message = QStringLiteral("No se pudo leer las bibliotecas del servidor."); setState(st); return; }
    const QString streamBase = m_base + QStringLiteral("/api/v1/media");
    QStringList movies, seriesIds, episodes;

    QSqlQuery insLib(c.db), insItem(c.db), insSerie(c.db), insSeason(c.db);
    insLib.prepare(QStringLiteral("INSERT INTO libraries (id, nombre, tipo, ruta_actual, creado_en) VALUES (?, ?, ?, ?, ?)"));
    insItem.prepare(QStringLiteral("INSERT INTO media_items (id, library_id, ruta_relativa, estado_archivo, tmdb_id, titulo, anio, poster_url, backdrop_url, duracion_segundos, generos, creado_en, series_id, numero_temporada, numero_episodio, sinopsis)"
                                   " VALUES (?, ?, ?, 'ok', ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    insSerie.prepare(QStringLiteral("INSERT INTO series (id, library_id, ruta_relativa, estado_archivo, tmdb_id, titulo, anio, poster_url, backdrop_url, generos, creado_en) VALUES (?, ?, ?, 'ok', ?, ?, ?, ?, ?, ?, ?)"));
    insSeason.prepare(QStringLiteral("INSERT OR IGNORE INTO seasons (id, series_id, numero_temporada, titulo, sinopsis, poster_url) VALUES (?, ?, ?, ?, ?, ?)"));
    auto itemRow = [&](const QJsonObject &m, const QString &lib, const QString &seriesId) -> QVariantList {
        const QVariant title = txt(m.value(QStringLiteral("titulo"))).isNull() ? txt(m.value(QStringLiteral("tituloArchivo"))) : txt(m.value(QStringLiteral("titulo")));
        return {m.value(QStringLiteral("id")).toString(), lib, m.value(QStringLiteral("id")).toString() + QStringLiteral("/stream?api_key=") + m_token, txt(m.value(QStringLiteral("tmdbId"))), title,
                num(m.value(QStringLiteral("anio"))), txt(m.value(QStringLiteral("posterUrl"))), txt(m.value(QStringLiteral("backdropUrl"))), num(m.value(QStringLiteral("duracionSegundos"))),
                genresJson(m.value(QStringLiteral("generos"))), secs(m.value(QStringLiteral("creadoEn"))), seriesId.isEmpty() ? nullVar() : QVariant(seriesId),
                num(m.value(QStringLiteral("numeroTemporada"))), num(m.value(QStringLiteral("numeroEpisodio"))), txt(m.value(QStringLiteral("sinopsis")))};
    };
    c.db.transaction();
    for (const QJsonValue &bv : libsV.toArray()) {
        if (m_cancelled) return;
        const QJsonObject b = bv.toObject();
        const QString lib = b.value(QStringLiteral("id")).toString();
        exec(insLib, {lib, b.value(QStringLiteral("nombre")).toString(), b.value(QStringLiteral("tipo")).toString(), streamBase, secs(b.value(QStringLiteral("creadoEn")))});
        if (b.value(QStringLiteral("tipo")).toString() == QLatin1String("pelicula")) {
            for (const QJsonValue &mv : api.get(QStringLiteral("/libraries/%1/items").arg(lib)).toArray()) { exec(insItem, itemRow(mv.toObject(), lib, QString())); movies << mv.toObject().value(QStringLiteral("id")).toString(); }
        } else {
            for (const QJsonValue &sv : api.get(QStringLiteral("/libraries/%1/series").arg(lib)).toArray()) {
                const QJsonObject s = sv.toObject();
                const QString sid = s.value(QStringLiteral("id")).toString();
                const QVariant title = txt(s.value(QStringLiteral("titulo"))).isNull() ? txt(s.value(QStringLiteral("tituloArchivo"))) : txt(s.value(QStringLiteral("titulo")));
                exec(insSerie, {sid, lib, sid, txt(s.value(QStringLiteral("tmdbId"))), title, num(s.value(QStringLiteral("anio"))), txt(s.value(QStringLiteral("posterUrl"))),
                                txt(s.value(QStringLiteral("backdropUrl"))), genresJson(s.value(QStringLiteral("generos"))), secs(s.value(QStringLiteral("creadoEn")))});
                seriesIds << sid;
                for (const QJsonValue &tv : api.get(QStringLiteral("/series/%1/seasons").arg(sid)).toArray()) {
                    const QJsonObject t = tv.toObject();
                    exec(insSeason, {t.value(QStringLiteral("id")).toString(), sid, num(t.value(QStringLiteral("numeroTemporada"))).isNull() ? 0 : num(t.value(QStringLiteral("numeroTemporada"))).toInt(),
                                     txt(t.value(QStringLiteral("titulo"))), txt(t.value(QStringLiteral("sinopsis"))), txt(t.value(QStringLiteral("posterUrl")))});
                }
                for (const QJsonValue &ev : api.get(QStringLiteral("/series/%1/episodes").arg(sid)).toArray()) { exec(insItem, itemRow(ev.toObject(), lib, sid)); episodes << ev.toObject().value(QStringLiteral("id")).toString(); }
            }
        }
        st.done = int(movies.size() + seriesIds.size());
        setState(st);
    }
    c.db.commit();
    copyPersonal(c.db, api);
    q.exec(QStringLiteral("PRAGMA wal_checkpoint(PASSIVE)"));
    q.finish();
    if (m_cancelled) return;

    // ---- fase 2: fichas completas (sinopsis, reparto…), en tandas con varias peticiones a la vez
    st.phase = QStringLiteral("fichas");
    st.done = 0;
    st.total = int(movies.size() + seriesIds.size());
    st.path = path;
    st.message = QStringLiteral("Descargando fichas completas…");
    setState(st);
    struct Job { QString route, table, id; };
    QList<Job> jobs;
    for (const QString &id : movies) jobs.append({QStringLiteral("/items/") + id, QStringLiteral("media_items"), id});
    for (const QString &id : seriesIds) jobs.append({QStringLiteral("/series/") + id, QStringLiteral("series"), id});
    QThreadPool pool;
    pool.setMaxThreadCount(10);
    const int batch = 40;
    for (int off = 0; off < jobs.size() && !m_cancelled; off += batch) {
        const QList<Job> part = jobs.mid(off, batch);
        const QList<QJsonValue> results = QtConcurrent::blockingMapped(&pool, part, [&api](const Job &j) { return api.get(j.route); });
        c.db.transaction();
        for (int i = 0; i < part.size(); ++i) {
            const QJsonObject d = results.at(i).toObject();
            if (d.isEmpty()) continue;
            QSqlQuery up(c.db);
            up.prepare(QStringLiteral("UPDATE %1 SET titulo = COALESCE(titulo, ?), sinopsis = ?, titulo_original = ?, idioma_original = ?, estudio = ?, reparto = ?, equipo = ? WHERE id = ?").arg(part.at(i).table));
            exec(up, {txt(d.value(QStringLiteral("tituloArchivo"))), txt(d.value(QStringLiteral("sinopsis"))), txt(d.value(QStringLiteral("tituloOriginal"))), txt(d.value(QStringLiteral("idiomaOriginal"))),
                      txt(d.value(QStringLiteral("estudio"))), genresJson(d.value(QStringLiteral("reparto"))), genresJson(d.value(QStringLiteral("equipo"))), part.at(i).id});
        }
        if (!c.db.commit()) qWarning().noquote() << "[espejo] commit:" << c.db.lastError().text();
        st.done = qMin(st.total, off + int(part.size()));
        setState(st);
    }
    if (m_cancelled) return;
    q.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
    q.finish();
    st.phase = QStringLiteral("listo");
    st.message.clear();
    setState(st);
}

void Mirror::updatePersonal()
{
    const State s = state();
    if (s.path.isEmpty() || s.phase == QLatin1String("listados")) return;
    Conn c;
    c.name = QStringLiteral("cl-espejo-p-%1").arg(quintptr(QThread::currentThreadId()));
    c.db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), c.name);
    c.db.setDatabaseName(s.path);
    c.db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (!c.db.open()) return;
    Remote::Dest dest;
    dest.host = QStringLiteral("127.0.0.1");
    dest.port = QUrl(m_base).port();
    QSqlQuery q(c.db);
    c.db.transaction();
    q.exec(QStringLiteral("DELETE FROM watch_progresses"));
    q.exec(QStringLiteral("DELETE FROM proximas_a_ver"));
    c.db.commit();
    copyPersonal(c.db, Api{dest, m_token});
}
