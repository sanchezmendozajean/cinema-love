#include "UserApi.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QUuid>

#include "Catalog.h"
#include "HttpServer.h"
#include "Mappers.h"
#include "Store.h"

using namespace Mappers;

namespace {

const QString kReadOnly = QStringLiteral("La base de datos está en solo lectura (es de una versión más nueva de CinemaLove).");

QVariant uid(const HttpRequest &r) { return r.userId.isEmpty() ? QVariant(QMetaType::fromType<QString>()) : QVariant(r.userId); }
qint64 now() { return QDateTime::currentSecsSinceEpoch(); }

QJsonObject progressJson(const QVariantMap &p)
{
    if (p.isEmpty()) return QJsonObject{{"posicionSegundos", 0}, {"estado", "noVisto"}};
    QJsonObject o{{"posicionSegundos", p["posicion_segundos"].toLongLong()}, {"estado", p["estado"].toString()}};
    if (!p["pista_audio_id"].toString().isEmpty()) o.insert(QStringLiteral("pistaAudioId"), p["pista_audio_id"].toString());
    if (!p["pista_subtitulo_id"].toString().isEmpty()) o.insert(QStringLiteral("pistaSubtituloId"), p["pista_subtitulo_id"].toString());
    return o;
}

struct ProgressBody { QString estado; qint64 pos = 0; QVariant audio, sub; };

/** Valida el cuerpo de un PUT de progreso; devuelve el mensaje de error o vacío. */
QString readProgress(const HttpRequest &r, ProgressBody *out)
{
    bool ok = false;
    const QJsonValue v = r.json(&ok);
    const QJsonObject b = (ok && v.isObject()) ? v.toObject() : QJsonObject();
    const QJsonValue e = b.value(QStringLiteral("estado"));
    if (!e.isUndefined() && !e.isNull()) {
        const QString s = e.isString() ? e.toString() : QString::fromUtf8(QJsonDocument(QJsonArray{e}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
        if (!e.isString() || (s != QLatin1String("noVisto") && s != QLatin1String("enProgreso") && s != QLatin1String("visto"))) return QStringLiteral("estado inválido: %1").arg(s);
        out->estado = s;
    }
    const QJsonValue p = b.value(QStringLiteral("posicionSegundos"));
    out->pos = p.isDouble() ? qMax<qint64>(0, qint64(p.toDouble())) : 0;
    out->audio = b.value(QStringLiteral("pistaAudioId")).isString() ? QVariant(b.value(QStringLiteral("pistaAudioId")).toString()) : QVariant(QMetaType::fromType<QString>());
    out->sub = b.value(QStringLiteral("pistaSubtituloId")).isString() ? QVariant(b.value(QStringLiteral("pistaSubtituloId")).toString()) : QVariant(QMetaType::fromType<QString>());
    return {};
}

bool itemExists(const QString &id) { return Store::scalar(QStringLiteral("SELECT 1 FROM media_items WHERE id = ?"), {id}).isValid(); }

// ---------------------------------------------------------------- progreso global

QVariantMap getProgress(const QString &itemId, const QVariant &u)
{
    return Store::row(QStringLiteral("SELECT * FROM watch_progresses WHERE media_item_id = ? AND usuario_id IS ?"), {itemId, u});
}

void saveProgress(const QString &itemId, const QVariant &u, qint64 pos, const QString &state, const QVariant &audio, const QVariant &sub)
{
    Store::begin();
    const QVariantMap ex = Store::row(QStringLiteral("SELECT id, pista_audio_id, pista_subtitulo_id FROM watch_progresses WHERE media_item_id = ? AND usuario_id IS ?"), {itemId, u});
    if (ex.isEmpty())
        Store::exec(QStringLiteral("INSERT INTO watch_progresses (media_item_id, usuario_id, posicion_segundos, estado, ultima_reproduccion, pista_audio_id, pista_subtitulo_id) VALUES (?, ?, ?, ?, ?, ?, ?)"),
                    {itemId, u, pos, state, now(), audio, sub});
    else
        Store::exec(QStringLiteral("UPDATE watch_progresses SET posicion_segundos = ?, estado = ?, ultima_reproduccion = ?, pista_audio_id = ?, pista_subtitulo_id = ? WHERE id = ?"),
                    {pos, state, now(), audio.isNull() ? ex["pista_audio_id"] : audio, sub.isNull() ? ex["pista_subtitulo_id"] : sub, ex["id"]});
    Store::commit();
}

void markSeen(const QString &itemId, const QVariant &u)
{
    Store::begin();
    saveProgress(itemId, u, 0, QStringLiteral("visto"), QVariant(QMetaType::fromType<QString>()), QVariant(QMetaType::fromType<QString>()));
    Store::exec(QStringLiteral("DELETE FROM proximas_a_ver WHERE media_item_id = ? AND usuario_id IS ?"), {itemId, u});
    Store::commit();
}

void markUnseen(const QString &itemId, const QVariant &u)
{
    Store::exec(QStringLiteral("DELETE FROM watch_progresses WHERE media_item_id = ? AND usuario_id IS ?"), {itemId, u});
}

// ---------------------------------------------------------------- Próximas a ver

bool inQueue(const QString &itemId, const QVariant &u)
{
    return Store::scalar(QStringLiteral("SELECT 1 FROM proximas_a_ver WHERE media_item_id = ? AND usuario_id IS ?"), {itemId, u}).isValid();
}

// ---------------------------------------------------------------- colecciones

QJsonObject collectionJson(const QVariantMap &c)
{
    QJsonArray posters;
    for (const QVariantMap &p : Store::rows(QStringLiteral(
             "SELECT m.poster_url FROM collection_items ci JOIN media_items m ON m.id = ci.media_item_id"
             " WHERE ci.collection_id = ? AND m.estado_archivo = 'ok' AND m.poster_url IS NOT NULL ORDER BY ci.agregado_en DESC, ci.id DESC LIMIT 4"), {c["id"]}))
        posters.append(p["poster_url"].toString());
    const qint64 count = Store::scalar(QStringLiteral(
        "SELECT COUNT(*) FROM collection_items ci JOIN media_items m ON m.id = ci.media_item_id WHERE ci.collection_id = ? AND m.estado_archivo = 'ok'"), {c["id"]}).toLongLong();
    return QJsonObject{{"id", c["id"].toString()}, {"nombre", c["nombre"].toString()}, {"quitarAlVerse", c["quitar_al_verse"].toInt() != 0},
                       {"creadoEn", isoDate(c["creado_en"].toLongLong())}, {"conteo", count}, {"posters", posters}};
}

QVariantMap getCollection(const QString &id, const QVariant &u)
{
    return Store::row(QStringLiteral("SELECT * FROM collections WHERE id = ? AND usuario_id IS ?"), {id, u});
}

QStringList collectionIds(const QString &cid)
{
    QStringList ids;
    for (const QVariantMap &r : Store::rows(QStringLiteral(
             "SELECT ci.media_item_id AS id FROM collection_items ci JOIN media_items m ON m.id = ci.media_item_id"
             " WHERE ci.collection_id = ? AND m.estado_archivo = 'ok' ORDER BY ci.agregado_en DESC, ci.id DESC"), {cid}))
        ids << r["id"].toString();
    return ids;
}

QVariantMap getCollProgress(const QString &cid, const QString &itemId, const QVariant &u)
{
    return Store::row(QStringLiteral("SELECT * FROM collection_watch_progresses WHERE collection_id = ? AND media_item_id = ? AND usuario_id IS ?"), {cid, itemId, u});
}

void saveCollProgress(const QString &cid, const QString &itemId, const QVariant &u, qint64 pos, const QString &state, const QVariant &audio, const QVariant &sub)
{
    Store::begin();
    const QVariantMap ex = Store::row(QStringLiteral("SELECT id, pista_audio_id, pista_subtitulo_id FROM collection_watch_progresses WHERE collection_id = ? AND media_item_id = ? AND usuario_id IS ?"), {cid, itemId, u});
    if (ex.isEmpty())
        Store::exec(QStringLiteral("INSERT INTO collection_watch_progresses (collection_id, media_item_id, usuario_id, posicion_segundos, estado, ultima_reproduccion, pista_audio_id, pista_subtitulo_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?)"),
                    {cid, itemId, u, pos, state, now(), audio, sub});
    else
        Store::exec(QStringLiteral("UPDATE collection_watch_progresses SET posicion_segundos = ?, estado = ?, ultima_reproduccion = ?, pista_audio_id = ?, pista_subtitulo_id = ? WHERE id = ?"),
                    {pos, state, now(), audio.isNull() ? ex["pista_audio_id"] : audio, sub.isNull() ? ex["pista_subtitulo_id"] : sub, ex["id"]});
    Store::commit();
}

HttpResponse noLists(const HttpRequest &r, bool *denied)
{
    *denied = r.principal == QLatin1String("token");
    return *denied ? HttpResponse::error(403, QStringLiteral("Las colecciones y \"Próximas a ver\" requieren conexión por usuario/contraseña, no por API key.")) : HttpResponse();
}

} // namespace

void registerUserRoutes(HttpServer &h)
{
    // --- progreso [v1-legado]
    h.route("GET", "/api/v1/media/:id/progress", [](HttpRequest &r) {
        if (!itemExists(r.params["id"])) return HttpResponse::error(404, QStringLiteral("Ítem no encontrado."));
        return HttpResponse::json(progressJson(getProgress(r.params["id"], uid(r))));
    });
    h.route("PUT", "/api/v1/media/:id/progress", [](HttpRequest &r) {
        const QString id = r.params["id"];
        if (!itemExists(id)) return HttpResponse::error(404, QStringLiteral("Ítem no encontrado."));
        ProgressBody p;
        const QString err = readProgress(r, &p);
        if (!err.isEmpty()) return HttpResponse::error(400, err);
        if (Store::readOnly()) return HttpResponse::error(409, kReadOnly);
        if (p.estado == QLatin1String("visto")) markSeen(id, uid(r));
        else if (p.estado == QLatin1String("noVisto")) markUnseen(id, uid(r));
        else saveProgress(id, uid(r), p.pos, QStringLiteral("enProgreso"), p.audio, p.sub);
        return HttpResponse::json(progressJson(getProgress(id, uid(r))));
    });

    // --- estados agregados
    h.route("GET", "/api/v1/libraries/:id/progress-states", [](HttpRequest &r) {
        if (!Store::scalar(QStringLiteral("SELECT 1 FROM libraries WHERE id = ?"), {r.params["id"]}).isValid()) return HttpResponse::error(404, QStringLiteral("Biblioteca no encontrada."));
        QJsonObject o;
        for (const QVariantMap &f : Store::rows(QStringLiteral(
                 "SELECT wp.media_item_id AS id, wp.estado FROM watch_progresses wp JOIN media_items m ON m.id = wp.media_item_id"
                 " WHERE m.library_id = ? AND wp.usuario_id IS ? AND wp.estado != 'noVisto'"), {r.params["id"], uid(r)}))
            o.insert(f["id"].toString(), f["estado"].toString());
        return HttpResponse::json(o);
    });
    h.route("GET", "/api/v1/series/:id/progress-states", [](HttpRequest &r) {
        if (!Store::scalar(QStringLiteral("SELECT 1 FROM series WHERE id = ?"), {r.params["id"]}).isValid()) return HttpResponse::error(404, QStringLiteral("Serie no encontrada."));
        QJsonObject o;
        for (const QVariantMap &f : Store::rows(QStringLiteral(
                 "SELECT wp.media_item_id AS id, wp.estado, wp.posicion_segundos AS pos FROM watch_progresses wp JOIN media_items m ON m.id = wp.media_item_id"
                 " WHERE m.series_id = ? AND wp.usuario_id IS ?"), {r.params["id"], uid(r)}))
            o.insert(f["id"].toString(), QJsonObject{{"estado", f["estado"].toString()}, {"posicionSegundos", f["pos"].toLongLong()}});
        return HttpResponse::json(o);
    });
    h.route("PUT", "/api/v1/series/:id/progress", [](HttpRequest &r) {
        const QString sid = r.params["id"];
        if (!Store::scalar(QStringLiteral("SELECT 1 FROM series WHERE id = ?"), {sid}).isValid()) return HttpResponse::error(404, QStringLiteral("Serie no encontrada."));
        bool ok = false;
        const QJsonValue v = r.json(&ok);
        const QJsonObject b = (ok && v.isObject()) ? v.toObject() : QJsonObject();
        const QString estado = b.value(QStringLiteral("estado")).toString();
        if (estado != QLatin1String("visto") && estado != QLatin1String("noVisto")) return HttpResponse::error(400, QStringLiteral("estado debe ser \"visto\" o \"noVisto\"."));
        const QJsonValue t = b.value(QStringLiteral("temporada"));
        if (Store::readOnly()) return HttpResponse::error(409, kReadOnly);
        QString sql = QStringLiteral("SELECT id FROM media_items WHERE series_id = ? AND estado_archivo = 'ok'");
        QVariantList binds{sid};
        if (t.isDouble()) { sql += QStringLiteral(" AND numero_temporada = ?"); binds << qint64(t.toDouble()); }
        QStringList ids;
        for (const QVariantMap &row : Store::rows(sql, binds)) ids << row["id"].toString();
        Store::begin();
        for (const QString &id : ids) { if (estado == QLatin1String("visto")) markSeen(id, uid(r)); else markUnseen(id, uid(r)); }
        Store::commit();
        return HttpResponse::json(QJsonObject{{"ok", true}, {"episodios", int(ids.size())}});
    });
    h.route("GET", "/api/v1/media/:id/user-state", [](HttpRequest &r) {
        const QString id = r.params["id"];
        if (!itemExists(id)) return HttpResponse::error(404, QStringLiteral("Ítem no encontrado."));
        const bool lists = r.principal != QLatin1String("token");
        QJsonArray cols;
        if (lists)
            for (const QVariantMap &c : Store::rows(QStringLiteral(
                     "SELECT c.* FROM collections c JOIN collection_items ci ON ci.collection_id = c.id WHERE ci.media_item_id = ? AND c.usuario_id IS ? ORDER BY c.nombre COLLATE NOCASE"), {id, uid(r)}))
                cols.append(QJsonObject{{"id", c["id"].toString()}, {"nombre", c["nombre"].toString()}});
        return HttpResponse::json(QJsonObject{{"progreso", progressJson(getProgress(id, uid(r)))}, {"enProximas", lists && inQueue(id, uid(r))}, {"colecciones", cols}});
    });

    // --- Próximas a ver [v1-legado]
    h.route("GET", "/api/v1/watchlist", [](HttpRequest &r) {
        bool denied;
        const HttpResponse d = noLists(r, &denied);
        if (denied) return d;
        QStringList ids;
        for (const QVariantMap &p : Store::rows(QStringLiteral("SELECT media_item_id FROM proximas_a_ver WHERE usuario_id IS ? ORDER BY agregado_en DESC"), {uid(r)})) ids << p["media_item_id"].toString();
        if (r.q(QStringLiteral("formato")) == QLatin1String("catalogo")) return HttpResponse::json(Catalog::elementsByIds(ids, r.userId, true));
        QJsonArray out;
        for (const QString &id : ids) {
            const QVariantMap m = Store::row(QStringLiteral("SELECT * FROM media_items WHERE id = ?"), {id});
            if (!m.isEmpty() && m["estado_archivo"].toString() == QLatin1String("ok")) out.append(mediaItem(m, false));
        }
        return HttpResponse::json(out);
    });
    h.route("PUT", "/api/v1/watchlist/:id", [](HttpRequest &r) {
        bool denied;
        const HttpResponse d = noLists(r, &denied);
        if (denied) return d;
        if (!itemExists(r.params["id"])) return HttpResponse::error(404, QStringLiteral("Ítem no encontrado."));
        if (Store::readOnly()) return HttpResponse::error(409, kReadOnly);
        if (!inQueue(r.params["id"], uid(r))) Store::exec(QStringLiteral("INSERT INTO proximas_a_ver (media_item_id, usuario_id, agregado_en) VALUES (?, ?, ?)"), {r.params["id"], uid(r), now()});
        return HttpResponse::json(QJsonObject{{"ok", true}});
    });
    h.route("DELETE", "/api/v1/watchlist/:id", [](HttpRequest &r) {
        bool denied;
        const HttpResponse d = noLists(r, &denied);
        if (denied) return d;
        if (Store::readOnly()) return HttpResponse::error(409, kReadOnly);
        Store::exec(QStringLiteral("DELETE FROM proximas_a_ver WHERE media_item_id = ? AND usuario_id IS ?"), {r.params["id"], uid(r)});
        return HttpResponse::json(QJsonObject{{"ok", true}});
    });

    // --- colecciones [v1-legado]
    h.route("GET", "/api/v1/collections", [](HttpRequest &r) {
        bool denied;
        const HttpResponse d = noLists(r, &denied);
        if (denied) return d;
        QJsonArray out;
        for (const QVariantMap &c : Store::rows(QStringLiteral("SELECT * FROM collections WHERE usuario_id IS ? ORDER BY nombre COLLATE NOCASE"), {uid(r)})) out.append(collectionJson(c));
        return HttpResponse::json(out);
    });
    h.route("POST", "/api/v1/collections", [](HttpRequest &r) {
        bool denied;
        const HttpResponse d = noLists(r, &denied);
        if (denied) return d;
        bool ok = false;
        const QJsonValue v = r.json(&ok);
        const QJsonObject b = (ok && v.isObject()) ? v.toObject() : QJsonObject();
        const QString name = b.value(QStringLiteral("nombre")).toString();
        if (!b.value(QStringLiteral("nombre")).isString() || name.trimmed().isEmpty()) return HttpResponse::error(400, QStringLiteral("nombre es requerido."));
        if (name.trimmed().size() > 200) return HttpResponse::error(400, QStringLiteral("El nombre admite hasta 200 caracteres."));
        if (Store::readOnly()) return HttpResponse::error(409, kReadOnly);
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        Store::exec(QStringLiteral("INSERT INTO collections (id, nombre, quitar_al_verse, usuario_id, creado_en) VALUES (?, ?, ?, ?, ?)"),
                    {id, name.trimmed(), b.value(QStringLiteral("quitarAlVerse")).toBool() ? 1 : 0, uid(r), now()});
        return HttpResponse::json(collectionJson(getCollection(id, uid(r))));
    });

    // Resuelve la colección del principal o responde 403/404.
    auto withCollection = [](HttpRequest &r, QVariantMap *c, HttpResponse *res) {
        bool denied;
        const HttpResponse d = noLists(r, &denied);
        if (denied) { *res = d; return false; }
        *c = getCollection(r.params["id"], uid(r));
        if (c->isEmpty()) { *res = HttpResponse::error(404, QStringLiteral("Colección no encontrada.")); return false; }
        return true;
    };

    h.route("GET", "/api/v1/collections/item/:id", [](HttpRequest &r) {
        bool denied;
        const HttpResponse d = noLists(r, &denied);
        if (denied) return d;
        QJsonArray out;
        for (const QVariantMap &c : Store::rows(QStringLiteral(
                 "SELECT c.* FROM collections c JOIN collection_items ci ON ci.collection_id = c.id WHERE ci.media_item_id = ? AND c.usuario_id IS ? ORDER BY c.nombre COLLATE NOCASE"), {r.params["id"], uid(r)}))
            out.append(collectionJson(c));
        return HttpResponse::json(out);
    });
    h.route("GET", "/api/v1/collections/:id", [withCollection](HttpRequest &r) {
        QVariantMap c; HttpResponse res;
        return withCollection(r, &c, &res) ? HttpResponse::json(collectionJson(c)) : res;
    });
    h.route("PUT", "/api/v1/collections/:id", [withCollection](HttpRequest &r) {
        QVariantMap c; HttpResponse res;
        if (!withCollection(r, &c, &res)) return res;
        bool ok = false;
        const QJsonValue v = r.json(&ok);
        const QJsonObject b = (ok && v.isObject()) ? v.toObject() : QJsonObject();
        const QJsonValue n = b.value(QStringLiteral("nombre"));
        if (n.isString() && (n.toString().trimmed().isEmpty() || n.toString().trimmed().size() > 200)) return HttpResponse::error(400, QStringLiteral("El nombre debe tener entre 1 y 200 caracteres."));
        if (Store::readOnly()) return HttpResponse::error(409, kReadOnly);
        if (n.isString()) Store::exec(QStringLiteral("UPDATE collections SET nombre = ? WHERE id = ?"), {n.toString().trimmed(), c["id"]});
        if (b.value(QStringLiteral("quitarAlVerse")).isBool()) Store::exec(QStringLiteral("UPDATE collections SET quitar_al_verse = ? WHERE id = ?"), {b.value(QStringLiteral("quitarAlVerse")).toBool() ? 1 : 0, c["id"]});
        return HttpResponse::json(collectionJson(getCollection(c["id"].toString(), uid(r))));
    });
    h.route("DELETE", "/api/v1/collections/:id", [withCollection](HttpRequest &r) {
        QVariantMap c; HttpResponse res;
        if (!withCollection(r, &c, &res)) return res;
        if (Store::readOnly()) return HttpResponse::error(409, kReadOnly);
        Store::exec(QStringLiteral("DELETE FROM collections WHERE id = ?"), {c["id"]});
        return HttpResponse::json(QJsonObject{{"ok", true}});
    });
    h.route("GET", "/api/v1/collections/:id/items", [withCollection](HttpRequest &r) {
        QVariantMap c; HttpResponse res;
        if (!withCollection(r, &c, &res)) return res;
        const QStringList ids = collectionIds(c["id"].toString());
        if (r.q(QStringLiteral("formato")) == QLatin1String("catalogo")) {
            // Dentro de una colección cada tarjeta muestra el progreso PROPIO de la colección (RF-62), no el global.
            QHash<QString, QVariantMap> own;
            for (const QVariantMap &p : Store::rows(QStringLiteral("SELECT * FROM collection_watch_progresses WHERE collection_id = ? AND usuario_id IS ?"), {c["id"], uid(r)})) own.insert(p["media_item_id"].toString(), p);
            QJsonArray out;
            for (const QJsonValue &ev : Catalog::elementsByIds(ids, r.userId, true)) {
                QJsonObject el = ev.toObject();
                if (el["kind"].toString() == QLatin1String("pelicula")) {
                    const QVariantMap p = own.value(el["id"].toString());
                    el.insert(QStringLiteral("progreso"), p.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(QJsonObject{{"estado", p["estado"].toString()}, {"posicionSegundos", p["posicion_segundos"].toLongLong()}}));
                }
                out.append(el);
            }
            return HttpResponse::json(out);
        }
        QJsonArray out;
        for (const QString &id : ids) {
            const QVariantMap m = Store::row(QStringLiteral("SELECT * FROM media_items WHERE id = ?"), {id});
            if (!m.isEmpty()) out.append(mediaItem(m, false));
        }
        return HttpResponse::json(out);
    });
    h.route("POST", "/api/v1/collections/:id/items", [withCollection](HttpRequest &r) {
        QVariantMap c; HttpResponse res;
        if (!withCollection(r, &c, &res)) return res;
        bool ok = false;
        const QJsonValue v = r.json(&ok);
        const QJsonValue mid = (ok && v.isObject()) ? v.toObject().value(QStringLiteral("mediaItemId")) : QJsonValue();
        if (!mid.isString()) return HttpResponse::error(400, QStringLiteral("mediaItemId es requerido."));
        if (!itemExists(mid.toString())) return HttpResponse::error(404, QStringLiteral("Ítem no encontrado."));
        if (Store::readOnly()) return HttpResponse::error(409, kReadOnly);
        Store::exec(QStringLiteral("INSERT OR IGNORE INTO collection_items (collection_id, media_item_id, agregado_en) VALUES (?, ?, ?)"), {c["id"], mid.toString(), now()});
        return HttpResponse::json(QJsonObject{{"ok", true}});
    });
    h.route("DELETE", "/api/v1/collections/:id/items/:mediaItemId", [withCollection](HttpRequest &r) {
        QVariantMap c; HttpResponse res;
        if (!withCollection(r, &c, &res)) return res;
        if (Store::readOnly()) return HttpResponse::error(409, kReadOnly);
        Store::exec(QStringLiteral("DELETE FROM collection_items WHERE collection_id = ? AND media_item_id = ?"), {c["id"], r.params["mediaItemId"]});
        return HttpResponse::json(QJsonObject{{"ok", true}});
    });
    h.route("GET", "/api/v1/collections/:id/continuar-viendo", [withCollection](HttpRequest &r) {
        QVariantMap c; HttpResponse res;
        if (!withCollection(r, &c, &res)) return res;
        QStringList ids;
        QHash<QString, qint64> pos;
        for (const QVariantMap &f : Store::rows(QStringLiteral(
                 "SELECT cwp.media_item_id AS id, cwp.posicion_segundos AS pos FROM collection_watch_progresses cwp JOIN media_items m ON m.id = cwp.media_item_id AND m.estado_archivo = 'ok'"
                 " WHERE cwp.collection_id = ? AND cwp.usuario_id IS ? AND cwp.estado = 'enProgreso' ORDER BY cwp.ultima_reproduccion DESC"), {c["id"], uid(r)})) {
            ids << f["id"].toString();
            pos.insert(f["id"].toString(), f["pos"].toLongLong());
        }
        QJsonArray out;
        if (r.q(QStringLiteral("formato")) == QLatin1String("catalogo")) {
            for (const QJsonValue &ev : Catalog::elementsByIds(ids, r.userId, true)) {
                QJsonObject el = ev.toObject();
                if (el["kind"].toString() == QLatin1String("pelicula")) el.insert(QStringLiteral("progreso"), QJsonObject{{"estado", "enProgreso"}, {"posicionSegundos", pos.value(el["id"].toString(), 0)}});
                out.append(el);
            }
            return HttpResponse::json(out);
        }
        for (const QString &id : ids) {
            const QVariantMap m = Store::row(QStringLiteral("SELECT * FROM media_items WHERE id = ?"), {id});
            if (m.isEmpty()) continue;
            QJsonObject o = mediaItem(m, false);
            o.insert(QStringLiteral("posicionSegundos"), pos.value(id, 0));
            out.append(o);
        }
        return HttpResponse::json(out);
    });
    h.route("GET", "/api/v1/collections/:id/progress/:mediaItemId", [withCollection](HttpRequest &r) {
        QVariantMap c; HttpResponse res;
        if (!withCollection(r, &c, &res)) return res;
        return HttpResponse::json(progressJson(getCollProgress(c["id"].toString(), r.params["mediaItemId"], uid(r))));
    });
    h.route("PUT", "/api/v1/collections/:id/progress/:mediaItemId", [withCollection](HttpRequest &r) {
        QVariantMap c; HttpResponse res;
        if (!withCollection(r, &c, &res)) return res;
        ProgressBody p;
        const QString err = readProgress(r, &p);
        if (!err.isEmpty()) return HttpResponse::error(400, err);
        if (Store::readOnly()) return HttpResponse::error(409, kReadOnly);
        const QString cid = c["id"].toString(), mid = r.params["mediaItemId"];
        if (p.estado == QLatin1String("visto")) {
            Store::begin();
            saveCollProgress(cid, mid, uid(r), 0, QStringLiteral("visto"), QVariant(QMetaType::fromType<QString>()), QVariant(QMetaType::fromType<QString>()));
            if (c["quitar_al_verse"].toInt() != 0) Store::exec(QStringLiteral("DELETE FROM collection_items WHERE collection_id = ? AND media_item_id = ?"), {cid, mid});
            Store::commit();
        } else if (p.estado == QLatin1String("noVisto")) {
            Store::exec(QStringLiteral("DELETE FROM collection_watch_progresses WHERE collection_id = ? AND media_item_id = ? AND usuario_id IS ?"), {cid, mid, uid(r)});
        } else {
            saveCollProgress(cid, mid, uid(r), p.pos, QStringLiteral("enProgreso"), p.audio, p.sub);
        }
        return HttpResponse::json(progressJson(getCollProgress(cid, mid, uid(r))));
    });

    // --- personas: títulos del catálogo donde aparece (reparto o equipo)
    h.route("GET", "/api/v1/people/:id/in-catalog", [](HttpRequest &r) {
        bool ok = false;
        const qlonglong pid = r.params["id"].toLongLong(&ok);
        if (!ok) return HttpResponse::error(400, QStringLiteral("El id de persona debe ser numérico."));
        const QString part = QStringLiteral(
            "SELECT '%1' AS kind, x.id AS id, x.titulo AS titulo, ("
            " SELECT COALESCE(json_extract(j.value, '$.rol'), json_extract(j.value, '$.personaje')) FROM json_each(CASE WHEN json_valid(x.reparto) THEN x.reparto ELSE '[]' END) j"
            " WHERE json_extract(j.value, '$.id') = ? LIMIT 1) AS rol_reparto, ("
            " SELECT 1 FROM json_each(CASE WHEN json_valid(x.equipo) THEN x.equipo ELSE '[]' END) k WHERE json_extract(k.value, '$.id') = ? LIMIT 1) AS en_equipo"
            " FROM %2 x WHERE %3 AND (EXISTS (SELECT 1 FROM json_each(CASE WHEN json_valid(x.reparto) THEN x.reparto ELSE '[]' END) j WHERE json_extract(j.value, '$.id') = ?)"
            " OR EXISTS (SELECT 1 FROM json_each(CASE WHEN json_valid(x.equipo) THEN x.equipo ELSE '[]' END) k WHERE json_extract(k.value, '$.id') = ?))");
        const QString sql = part.arg(QStringLiteral("pelicula"), QStringLiteral("media_items"), QStringLiteral("x.series_id IS NULL AND x.estado_archivo = 'ok'"))
                            + QStringLiteral(" UNION ALL ")
                            + part.arg(QStringLiteral("serie"), QStringLiteral("series"), QStringLiteral("x.estado_archivo = 'ok'")) + QStringLiteral(" ORDER BY 3 COLLATE NOCASE");
        const QVariantList binds{pid, pid, pid, pid, pid, pid, pid, pid};
        QStringList ids;
        QHash<QString, QString> roles;
        for (const QVariantMap &f : Store::rows(sql, binds)) {
            ids << f["id"].toString();
            if (!f["rol_reparto"].toString().isEmpty()) roles.insert(f["id"].toString(), f["rol_reparto"].toString());
        }
        QJsonArray out;
        for (const QJsonValue &ev : Catalog::elementsByIds(ids, r.userId, false)) {
            QJsonObject el = ev.toObject();
            if (roles.contains(el["id"].toString())) el.insert(QStringLiteral("rol"), roles.value(el["id"].toString()));
            out.append(el);
        }
        return HttpResponse::json(out);
    });
}
