#include "Personal.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QUrl>

namespace {

PersonalResponse ok(const QJsonValue &v) { return {200, v}; }
PersonalResponse err(int status, const QString &m) { return {status, QJsonObject{{"error", m}}}; }

QJsonObject progressJson(const LocalProgress &p)
{
    if (!p.exists) return QJsonObject{{"posicionSegundos", 0}, {"estado", "noVisto"}};
    QJsonObject o{{"posicionSegundos", double(p.position)}, {"estado", p.state}};
    if (!p.audio.isEmpty()) o.insert(QStringLiteral("pistaAudioId"), p.audio);
    if (!p.sub.isEmpty()) o.insert(QStringLiteral("pistaSubtituloId"), p.sub);
    return o;
}

QJsonValue summary(const LocalProgress &p)
{
    return p.exists && p.state != QLatin1String("noVisto") ? QJsonValue(QJsonObject{{"estado", p.state}, {"posicionSegundos", double(p.position)}}) : QJsonValue(QJsonValue::Null);
}

struct PutProgress { QString state; qint64 pos = 0; QVariant audio, sub; };

/** Valida el cuerpo de un PUT de progreso, como el servidor. Devuelve el mensaje de error o vacío. */
QString readProgress(const QJsonValue &body, PutProgress *out)
{
    const QJsonObject b = body.isObject() ? body.toObject() : QJsonObject();
    const QJsonValue e = b.value(QStringLiteral("estado"));
    if (!e.isUndefined() && !e.isNull()) {
        const QString s = e.toString();
        if (!e.isString() || (s != QLatin1String("noVisto") && s != QLatin1String("enProgreso") && s != QLatin1String("visto"))) return QStringLiteral("estado inválido: %1").arg(e.isString() ? s : QStringLiteral("?"));
        out->state = s;
    }
    const QJsonValue p = b.value(QStringLiteral("posicionSegundos"));
    out->pos = p.isDouble() ? qMax<qint64>(0, qint64(p.toDouble())) : 0;
    out->audio = b.value(QStringLiteral("pistaAudioId")).isString() ? QVariant(b.value(QStringLiteral("pistaAudioId")).toString()) : QVariant();
    out->sub = b.value(QStringLiteral("pistaSubtituloId")).isString() ? QVariant(b.value(QStringLiteral("pistaSubtituloId")).toString()) : QVariant();
    return {};
}

QString isoDate(qint64 s) { return QDateTime::fromSecsSinceEpoch(s, Qt::UTC).toString(Qt::ISODateWithMs); }

}

Personal::Kind Personal::classify(const QString &method, const QString &path)
{
    static const QRegularExpression inCatalog(QStringLiteral(R"(^/people/[^/]+/in-catalog$)")), mediaP(QStringLiteral(R"(^/media/[^/]+/(progress|user-state)$)")),
        states(QStringLiteral(R"(^/(libraries|series)/[^/]+/progress-states$)")), seriesP(QStringLiteral(R"(^/series/[^/]+/progress$)"));
    if (method == QLatin1String("GET") && (path == QLatin1String("/home") || path == QLatin1String("/catalog"))) return Kind::Query;
    if (method == QLatin1String("GET") && inCatalog.match(path).hasMatch()) return Kind::Overlay;
    if (mediaP.match(path).hasMatch()) return Kind::Local;
    if (method == QLatin1String("GET") && states.match(path).hasMatch()) return Kind::Local;
    if (method == QLatin1String("PUT") && seriesP.match(path).hasMatch()) return Kind::Local;
    if (path == QLatin1String("/watchlist") || path.startsWith(QLatin1String("/watchlist/"))) return Kind::Local;
    if (path == QLatin1String("/collections") || path.startsWith(QLatin1String("/collections/"))) return Kind::Local;
    return Kind::None;
}

// El servidor responde /catalog, /home y las tarjetas viendo el progreso que se le manda (el de este equipo) y no el de su dueño.
QJsonValue Personal::withMyProgress(const QString &what, const QJsonObject &extra)
{
    QJsonArray states;
    for (const LocalState::Row &r : m_local.rows())
        states.append(QJsonObject{{"id", r.id}, {"estado", r.state}, {"posicionSegundos", double(r.position)}, {"ultima", r.last.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(r.last.toLongLong())}});
    QJsonObject body = extra;
    body.insert(QStringLiteral("consulta"), what);
    body.insert(QStringLiteral("estados"), states);
    const QJsonValue b(body);
    return m_remote(QStringLiteral("/catalog/con-estado"), &b);
}

QJsonArray Personal::elements(const QStringList &ids, const std::function<LocalProgress(const QString &)> &progressOf)
{
    QStringList list;
    for (const QString &i : ids) if (!i.isEmpty()) list << i;
    if (list.isEmpty()) return {};
    const QJsonValue r = withMyProgress(QStringLiteral("items"), QJsonObject{{"ids", QJsonArray::fromStringList(list)}});
    if (!r.isArray()) return {};
    if (!progressOf) return r.toArray();
    // Dentro de una colección cada película muestra el progreso PROPIO de la colección, no el global.
    QJsonArray out;
    for (const QJsonValue &ev : r.toArray()) {
        QJsonObject e = ev.toObject();
        if (e.value(QStringLiteral("kind")).toString() == QLatin1String("pelicula")) e.insert(QStringLiteral("progreso"), summary(progressOf(e.value(QStringLiteral("id")).toString())));
        out.append(e);
    }
    return out;
}

PersonalResponse Personal::query(const QString &path, const QUrlQuery &q)
{
    if (path == QLatin1String("/catalog")) {
        QJsonObject qo;
        for (const auto &kv : q.queryItems(QUrl::FullyDecoded)) qo.insert(kv.first, kv.second);
        const QJsonValue r = withMyProgress(QStringLiteral("catalog"), QJsonObject{{"query", qo}});
        return r.isNull() || r.isUndefined() ? err(502, QStringLiteral("No pudimos consultar el catálogo del servidor.")) : ok(r);
    }
    const QJsonValue h = withMyProgress(QStringLiteral("home"), {});
    if (!h.isObject()) return err(502, QStringLiteral("No pudimos consultar el inicio del servidor."));
    // «Próximas a ver» del servidor es la de su dueño: aquí la lista es la de este equipo.
    QJsonObject o = h.toObject();
    o.insert(QStringLiteral("proximasAVer"), elements(m_local.queue(30)));
    return ok(o);
}

QJsonValue Personal::overlay(const QJsonValue &body)
{
    if (!body.isArray()) return body;
    const QJsonArray originals = body.toArray();
    QStringList ids;
    for (const QJsonValue &v : originals) ids << v.toObject().value(QStringLiteral("id")).toString();
    QHash<QString, QJsonObject> mine;
    for (const QJsonValue &v : elements(ids)) mine.insert(v.toObject().value(QStringLiteral("id")).toString(), v.toObject());
    QJsonArray out;
    for (const QJsonValue &ov : originals) {
        const QJsonObject o = ov.toObject();
        if (!mine.contains(o.value(QStringLiteral("id")).toString())) continue;
        QJsonObject e = mine.value(o.value(QStringLiteral("id")).toString());
        if (!o.value(QStringLiteral("rol")).toString().isEmpty()) e.insert(QStringLiteral("rol"), o.value(QStringLiteral("rol")));
        out.append(e);
    }
    return out;
}

PersonalResponse Personal::handle(const QString &method, const QString &path, const QUrlQuery &q, const QJsonValue &body)
{
    const QStringList seg = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (seg.isEmpty()) return err(404, QStringLiteral("Ruta no encontrada."));

    // /media/:id/progress  y  /media/:id/user-state
    if (seg.at(0) == QLatin1String("media")) {
        const QString id = seg.value(1);
        if (seg.value(2) == QLatin1String("user-state")) {
            if (method != QLatin1String("GET")) return err(405, QStringLiteral("Método no permitido."));
            QJsonArray cols;
            for (const auto &c : m_local.collectionsOfItem(id)) cols.append(QJsonObject{{"id", c.id}, {"nombre", c.name}});
            return ok(QJsonObject{{"progreso", progressJson(m_local.progress(id))}, {"enProximas", m_local.inQueue(id)}, {"colecciones", cols}});
        }
        if (method == QLatin1String("GET")) return ok(progressJson(m_local.progress(id)));
        if (method != QLatin1String("PUT")) return err(405, QStringLiteral("Método no permitido."));
        PutProgress pr;
        const QString e = readProgress(body, &pr);
        if (!e.isEmpty()) return err(400, e);
        if (pr.state == QLatin1String("visto")) m_local.markSeen(id);
        else if (pr.state == QLatin1String("noVisto")) m_local.markUnseen(id);
        else m_local.saveProgress(id, pr.pos, QStringLiteral("enProgreso"), pr.audio, pr.sub);
        return ok(progressJson(m_local.progress(id)));
    }

    if (seg.at(0) == QLatin1String("libraries")) {
        // Las claves son ids de ítem, únicos en el servidor: devolver todo lo guardado sirve igual que filtrar por biblioteca.
        QJsonObject o;
        const auto all = m_local.states();
        for (auto it = all.begin(); it != all.end(); ++it) o.insert(it.key(), it.value().first);
        return ok(o);
    }

    if (seg.at(0) == QLatin1String("series")) {
        if (seg.value(2) == QLatin1String("progress-states")) {
            QJsonObject o;
            const auto all = m_local.states();
            for (auto it = all.begin(); it != all.end(); ++it) o.insert(it.key(), QJsonObject{{"estado", it.value().first}, {"posicionSegundos", double(it.value().second)}});
            return ok(o);
        }
        const QJsonObject b = body.isObject() ? body.toObject() : QJsonObject();
        const QString state = b.value(QStringLiteral("estado")).toString();
        if (state != QLatin1String("visto") && state != QLatin1String("noVisto")) return err(400, QStringLiteral("estado debe ser \"visto\" o \"noVisto\"."));
        const QJsonValue t = b.value(QStringLiteral("temporada"));
        const QString route = QStringLiteral("/series/%1/episodes").arg(seg.value(1)) + (t.isDouble() ? QStringLiteral("?temporada=%1").arg(qint64(t.toDouble())) : QString());
        const QJsonValue eps = m_remote(route, nullptr);
        if (!eps.isArray()) return err(502, QStringLiteral("No pudimos consultar los episodios de la serie."));
        QStringList ids;
        for (const QJsonValue &e : eps.toArray()) ids << e.toObject().value(QStringLiteral("id")).toString();
        m_local.markMany(ids, state);
        return ok(QJsonObject{{"ok", true}, {"episodios", int(ids.size())}});
    }

    if (seg.at(0) == QLatin1String("watchlist")) return queue(method, seg.value(1), q);
    return collections(method, seg, q, body);
}

PersonalResponse Personal::queue(const QString &method, const QString &id, const QUrlQuery &q)
{
    if (method == QLatin1String("GET") && id.isEmpty()) {
        const QStringList ids = m_local.queue();
        if (q.queryItemValue(QStringLiteral("formato")) == QLatin1String("catalogo")) return ok(elements(ids));
        const QJsonValue r = m_remote(QStringLiteral("/catalog/items?ids=") + QString::fromLatin1(QUrl::toPercentEncoding(ids.join(QLatin1Char(',')))), nullptr);
        return ok(r.isArray() ? r : QJsonValue(QJsonArray()));
    }
    if (id.isEmpty()) return err(405, QStringLiteral("Método no permitido."));
    if (method == QLatin1String("PUT")) m_local.addToQueue(id);
    else if (method == QLatin1String("DELETE")) m_local.removeFromQueue(id);
    else return err(405, QStringLiteral("Método no permitido."));
    return ok(QJsonObject{{"ok", true}});
}

QJsonArray Personal::collectionsJson(const QVector<LocalState::Collection> &cs)
{
    QList<QStringList> ids;
    QStringList firsts;
    for (const auto &c : cs) {
        const QStringList l = m_local.collectionItems(c.id);
        ids.append(l);
        for (const QString &i : l.mid(0, 4)) if (!firsts.contains(i)) firsts << i;
    }
    QHash<QString, QJsonValue> poster;
    for (const QJsonValue &e : elements(firsts)) poster.insert(e.toObject().value(QStringLiteral("id")).toString(), e.toObject().value(QStringLiteral("posterUrl")));
    QJsonArray out;
    for (int i = 0; i < cs.size(); ++i) {
        QJsonArray posters;
        for (const QString &id : ids.at(i).mid(0, 4)) { const QJsonValue p = poster.value(id); if (p.isString() && !p.toString().isEmpty()) posters.append(p); }
        out.append(QJsonObject{{"id", cs.at(i).id}, {"nombre", cs.at(i).name}, {"quitarAlVerse", cs.at(i).removeWhenSeen}, {"creadoEn", isoDate(cs.at(i).createdAt)},
                               {"conteo", int(ids.at(i).size())}, {"posters", posters}});
    }
    return out;
}

PersonalResponse Personal::collections(const QString &method, const QStringList &seg, const QUrlQuery &q, const QJsonValue &bodyV)
{
    const QJsonObject body = bodyV.isObject() ? bodyV.toObject() : QJsonObject();
    if (seg.value(1).isEmpty()) {
        if (method == QLatin1String("GET")) return ok(collectionsJson(m_local.collections()));
        if (method != QLatin1String("POST")) return err(405, QStringLiteral("Método no permitido."));
        const QString name = body.value(QStringLiteral("nombre")).toString();
        if (!body.value(QStringLiteral("nombre")).isString() || name.trimmed().isEmpty()) return err(400, QStringLiteral("nombre es requerido."));
        if (name.trimmed().size() > 200) return err(400, QStringLiteral("El nombre admite hasta 200 caracteres."));
        const QString id = m_local.createCollection(name, body.value(QStringLiteral("quitarAlVerse")).toBool());
        return ok(collectionsJson({m_local.collection(id)}).first());
    }
    if (seg.at(1) == QLatin1String("item")) {
        if (method != QLatin1String("GET") || seg.value(2).isEmpty()) return err(405, QStringLiteral("Método no permitido."));
        return ok(collectionsJson(m_local.collectionsOfItem(seg.at(2))));
    }
    const LocalState::Collection c = m_local.collection(seg.at(1));
    if (!c.valid()) return err(404, QStringLiteral("Colección no encontrada."));
    const QString sub = seg.value(2);

    if (sub.isEmpty()) {
        if (method == QLatin1String("GET")) return ok(collectionsJson({c}).first());
        if (method == QLatin1String("DELETE")) { m_local.deleteCollection(c.id); return ok(QJsonObject{{"ok", true}}); }
        if (method != QLatin1String("PUT")) return err(405, QStringLiteral("Método no permitido."));
        const QJsonValue n = body.value(QStringLiteral("nombre"));
        if (n.isString() && (n.toString().trimmed().isEmpty() || n.toString().trimmed().size() > 200)) return err(400, QStringLiteral("El nombre debe tener entre 1 y 200 caracteres."));
        if (n.isString()) m_local.renameCollection(c.id, n.toString());
        if (body.value(QStringLiteral("quitarAlVerse")).isBool()) m_local.setRemoveWhenSeen(c.id, body.value(QStringLiteral("quitarAlVerse")).toBool());
        return ok(collectionsJson({m_local.collection(c.id)}).first());
    }
    if (sub == QLatin1String("items")) {
        if (method == QLatin1String("GET")) {
            const QStringList ids = m_local.collectionItems(c.id);
            // Dentro de una colección cada tarjeta muestra el progreso PROPIO de la colección, no el global (RF-62).
            const auto own = m_local.collectionProgresses(c.id);
            if (q.queryItemValue(QStringLiteral("formato")) == QLatin1String("catalogo")) return ok(elements(ids, [own](const QString &id) { return own.value(id); }));
            const QJsonValue r = m_remote(QStringLiteral("/catalog/items?ids=") + QString::fromLatin1(QUrl::toPercentEncoding(ids.join(QLatin1Char(',')))), nullptr);
            return ok(r.isArray() ? r : QJsonValue(QJsonArray()));
        }
        if (method == QLatin1String("POST")) {
            if (!body.value(QStringLiteral("mediaItemId")).isString()) return err(400, QStringLiteral("mediaItemId es requerido."));
            m_local.addToCollection(c.id, body.value(QStringLiteral("mediaItemId")).toString());
            return ok(QJsonObject{{"ok", true}});
        }
        if (method == QLatin1String("DELETE") && !seg.value(3).isEmpty()) { m_local.removeFromCollection(c.id, seg.at(3)); return ok(QJsonObject{{"ok", true}}); }
        return err(405, QStringLiteral("Método no permitido."));
    }
    if (sub == QLatin1String("continuar-viendo") && method == QLatin1String("GET")) {
        const auto rows = m_local.continueInCollection(c.id);
        QHash<QString, qint64> pos;
        QStringList ids;
        for (const auto &r : rows) { ids << r.first; pos.insert(r.first, r.second); }
        return ok(elements(ids, [pos](const QString &id) { LocalProgress p; p.exists = true; p.state = QStringLiteral("enProgreso"); p.position = pos.value(id, 0); return p; }));
    }
    if (sub == QLatin1String("progress") && !seg.value(3).isEmpty()) {
        const QString id = seg.at(3);
        if (method == QLatin1String("GET")) return ok(progressJson(m_local.collectionProgress(c.id, id)));
        if (method != QLatin1String("PUT")) return err(405, QStringLiteral("Método no permitido."));
        PutProgress pr;
        const QString e = readProgress(body, &pr);
        if (!e.isEmpty()) return err(400, e);
        if (pr.state == QLatin1String("visto")) m_local.markSeenInCollection(c.id, id);
        else if (pr.state == QLatin1String("noVisto")) m_local.markUnseenInCollection(c.id, id);
        else m_local.saveCollectionProgress(c.id, id, pr.pos, QStringLiteral("enProgreso"), pr.audio, pr.sub);
        return ok(progressJson(m_local.collectionProgress(c.id, id)));
    }
    return err(404, QStringLiteral("Ruta no encontrada."));
}
