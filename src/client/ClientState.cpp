#include "ClientState.h"

#include <QDateTime>
#include <QUuid>

namespace {

qint64 now() { return QDateTime::currentSecsSinceEpoch(); }
QVariant nullVar() { return QVariant(QMetaType::fromType<QString>()); }

SavedServer toServer(const QVariantMap &f)
{
    SavedServer s;
    s.id = f["id"].toString();
    s.host = f["host"].toString();
    s.port = f["puerto"].toInt();
    s.https = f["https"].toInt() == 1;
    s.credentialType = f["tipo_credencial"].toString() == QLatin1String("sesion") ? QStringLiteral("sesion") : QStringLiteral("apiKey");   // NULL = apiKey (como en Flutter)
    s.apiKey = f["api_key"].toString();
    s.lastUser = f["ultimo_usuario_sesion"].toString();
    s.sessionToken = f["sesion_token_persistente"].toString();
    s.sessionExpires = f["expira_sesion_persistente"].isNull() ? 0 : f["expira_sesion_persistente"].toLongLong();
    s.trustCert = f["confiar_en_certificado"].toInt() == 1;
    s.name = f["nombre_servidor"].toString();
    s.serverUuid = f["server_uuid"].toString();
    s.fingerprint = f["huella_cert_sha256"].toString();
    s.certPem = f["cert_pem"].toString();
    return s;
}

const QString kSelect = QStringLiteral("SELECT s.*, e.nombre_servidor, e.server_uuid, e.huella_cert_sha256, e.cert_pem FROM cliente_servidores s LEFT JOIN cl_cliente_servidor_extra e ON e.servidor_id = s.id");

LocalProgress toProgress(const QVariantMap &r)
{
    LocalProgress p;
    if (r.isEmpty()) return p;
    p.exists = true;
    p.position = r["posicion_segundos"].toLongLong();
    p.state = r["estado"].toString();
    p.audio = r["pista_audio_id"].toString();
    p.sub = r["pista_subtitulo_id"].toString();
    return p;
}

}

// ================================================================== Servers

QVector<SavedServer> Servers::list() const
{
    QVector<SavedServer> out;
    for (const QVariantMap &f : m_db.rows(kSelect + QStringLiteral(" ORDER BY s.ultima_conexion DESC"))) out.append(toServer(f));
    return out;
}

SavedServer Servers::get(const QString &id) const
{
    const QVariantMap f = m_db.row(kSelect + QStringLiteral(" WHERE s.id = ?"), {id});
    return f.isEmpty() ? SavedServer() : toServer(f);
}

SavedServer Servers::byAddress(const QString &host, int port) const
{
    const QVariantMap f = m_db.row(kSelect + QStringLiteral(" WHERE s.host = ? AND s.puerto = ?"), {host, port});
    return f.isEmpty() ? SavedServer() : toServer(f);
}

SavedServer Servers::save(const QString &host, int port, bool https, const QVariantMap &d)
{
    const SavedServer prev = byAddress(host, port);
    const QString id = prev.valid() ? prev.id : QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto pick = [&](const QString &key, const QVariant &previous) { return d.contains(key) ? d.value(key) : previous; };
    auto nv = [](const QString &s) { return s.isEmpty() ? nullVar() : QVariant(s); };
    m_db.begin();
    m_db.exec(QStringLiteral("INSERT INTO cliente_servidores (id, host, puerto, ultima_conexion, api_key, tipo_credencial, ultimo_usuario_sesion, https, confiar_en_certificado, sesion_token_persistente, expira_sesion_persistente)"
                             " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) ON CONFLICT(id) DO UPDATE SET ultima_conexion = excluded.ultima_conexion, api_key = excluded.api_key,"
                             " tipo_credencial = excluded.tipo_credencial, ultimo_usuario_sesion = excluded.ultimo_usuario_sesion, https = excluded.https,"
                             " confiar_en_certificado = excluded.confiar_en_certificado, sesion_token_persistente = excluded.sesion_token_persistente,"
                             " expira_sesion_persistente = excluded.expira_sesion_persistente"),
              {id, host, port, now(), pick(QStringLiteral("apiKey"), nv(prev.apiKey)), pick(QStringLiteral("credentialType"), prev.valid() ? QVariant(prev.credentialType) : QVariant(QStringLiteral("apiKey"))),
               pick(QStringLiteral("lastUser"), nv(prev.lastUser)), https ? 1 : 0, pick(QStringLiteral("trustCert"), QVariant(prev.trustCert)).toBool() ? 1 : 0,
               pick(QStringLiteral("sessionToken"), nv(prev.sessionToken)), pick(QStringLiteral("sessionExpires"), prev.sessionExpires ? QVariant(prev.sessionExpires) : nullVar())});
    m_db.exec(QStringLiteral("INSERT INTO cl_cliente_servidor_extra (servidor_id, nombre_servidor, server_uuid, huella_cert_sha256, cert_pem) VALUES (?, ?, ?, ?, ?)"
                             " ON CONFLICT(servidor_id) DO UPDATE SET nombre_servidor = excluded.nombre_servidor, server_uuid = excluded.server_uuid,"
                             " huella_cert_sha256 = excluded.huella_cert_sha256, cert_pem = excluded.cert_pem"),
              {id, pick(QStringLiteral("name"), nv(prev.name)), pick(QStringLiteral("serverUuid"), nv(prev.serverUuid)), pick(QStringLiteral("fingerprint"), nv(prev.fingerprint)),
               pick(QStringLiteral("certPem"), nv(prev.certPem))});
    m_db.commit();
    return get(id);
}

void Servers::remove(const QString &id) const { m_db.exec(QStringLiteral("DELETE FROM cliente_servidores WHERE id = ?"), {id}); }

void Servers::forgetSession(const QString &id) const
{
    m_db.exec(QStringLiteral("UPDATE cliente_servidores SET sesion_token_persistente = NULL, expira_sesion_persistente = NULL WHERE id = ?"), {id});
}

// ================================================================== LocalState

LocalProgress LocalState::progress(const QString &itemId) const
{
    return toProgress(m_db->row(QStringLiteral("SELECT * FROM cliente_progresos WHERE servidor_id = ? AND media_item_id = ?"), {m_server, itemId}));
}

void LocalState::saveProgress(const QString &itemId, qint64 pos, const QString &state, const QVariant &audio, const QVariant &sub) const
{
    m_db->exec(QStringLiteral("INSERT INTO cliente_progresos (servidor_id, media_item_id, posicion_segundos, estado, ultima_reproduccion, pista_audio_id, pista_subtitulo_id) VALUES (?, ?, ?, ?, ?, ?, ?)"
                             " ON CONFLICT (servidor_id, media_item_id) DO UPDATE SET posicion_segundos = excluded.posicion_segundos, estado = excluded.estado, ultima_reproduccion = excluded.ultima_reproduccion,"
                             " pista_audio_id = COALESCE(excluded.pista_audio_id, pista_audio_id), pista_subtitulo_id = COALESCE(excluded.pista_subtitulo_id, pista_subtitulo_id)"),
              {m_server, itemId, pos, state, now(), audio.isNull() ? nullVar() : audio, sub.isNull() ? nullVar() : sub});
}

void LocalState::markSeen(const QString &itemId) const
{
    m_db->begin();
    saveProgress(itemId, 0, QStringLiteral("visto"));
    removeFromQueue(itemId);
    m_db->commit();
}

void LocalState::markUnseen(const QString &itemId) const { m_db->exec(QStringLiteral("DELETE FROM cliente_progresos WHERE servidor_id = ? AND media_item_id = ?"), {m_server, itemId}); }

void LocalState::markMany(const QStringList &ids, const QString &state) const
{
    m_db->begin();
    for (const QString &id : ids) { if (state == QLatin1String("visto")) markSeen(id); else markUnseen(id); }
    m_db->commit();
}

QMap<QString, QPair<QString, qint64>> LocalState::states() const
{
    QMap<QString, QPair<QString, qint64>> out;
    for (const QVariantMap &f : m_db->rows(QStringLiteral("SELECT media_item_id AS id, estado, posicion_segundos AS pos FROM cliente_progresos WHERE servidor_id = ? AND estado != 'noVisto'"), {m_server}))
        out.insert(f["id"].toString(), {f["estado"].toString(), f["pos"].toLongLong()});
    return out;
}

QVector<LocalState::Row> LocalState::rows() const
{
    QVector<Row> out;
    for (const QVariantMap &f : m_db->rows(QStringLiteral("SELECT media_item_id AS id, estado, posicion_segundos AS pos, ultima_reproduccion AS ultima FROM cliente_progresos WHERE servidor_id = ? AND estado != 'noVisto'"), {m_server}))
        out.append({f["id"].toString(), f["estado"].toString(), f["pos"].toLongLong(), f["ultima"]});
    return out;
}

bool LocalState::inQueue(const QString &itemId) const { return m_db->scalar(QStringLiteral("SELECT 1 FROM cliente_proximas_a_ver WHERE servidor_id = ? AND media_item_id = ?"), {m_server, itemId}).isValid(); }
void LocalState::addToQueue(const QString &itemId) const { m_db->exec(QStringLiteral("INSERT OR IGNORE INTO cliente_proximas_a_ver (servidor_id, media_item_id, agregado_en) VALUES (?, ?, ?)"), {m_server, itemId, now()}); }
void LocalState::removeFromQueue(const QString &itemId) const { m_db->exec(QStringLiteral("DELETE FROM cliente_proximas_a_ver WHERE servidor_id = ? AND media_item_id = ?"), {m_server, itemId}); }

QStringList LocalState::queue(int limit) const
{
    QStringList out;
    for (const QVariantMap &f : m_db->rows(QStringLiteral("SELECT media_item_id AS id FROM cliente_proximas_a_ver WHERE servidor_id = ? ORDER BY agregado_en DESC, id DESC LIMIT ?"), {m_server, limit})) out << f["id"].toString();
    return out;
}

namespace {
LocalState::Collection toCollection(const QVariantMap &r)
{
    LocalState::Collection c;
    if (r.isEmpty()) return c;
    c.id = r["id"].toString();
    c.name = r["nombre"].toString();
    c.removeWhenSeen = r["quitar_al_verse"].toInt() == 1;
    c.createdAt = r["creado_en"].toLongLong();
    return c;
}
}

QVector<LocalState::Collection> LocalState::collections() const
{
    QVector<Collection> out;
    for (const QVariantMap &r : m_db->rows(QStringLiteral("SELECT * FROM cliente_colecciones WHERE servidor_id = ? ORDER BY nombre COLLATE NOCASE"), {m_server})) out.append(toCollection(r));
    return out;
}

LocalState::Collection LocalState::collection(const QString &id) const
{
    return toCollection(m_db->row(QStringLiteral("SELECT * FROM cliente_colecciones WHERE id = ? AND servidor_id = ?"), {id, m_server}));
}

QString LocalState::createCollection(const QString &name, bool removeWhenSeen) const
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_db->exec(QStringLiteral("INSERT INTO cliente_colecciones (id, servidor_id, nombre, quitar_al_verse, creado_en) VALUES (?, ?, ?, ?, ?)"), {id, m_server, name.trimmed(), removeWhenSeen ? 1 : 0, now()});
    return id;
}

void LocalState::renameCollection(const QString &id, const QString &name) const { m_db->exec(QStringLiteral("UPDATE cliente_colecciones SET nombre = ? WHERE id = ? AND servidor_id = ?"), {name.trimmed(), id, m_server}); }
void LocalState::setRemoveWhenSeen(const QString &id, bool v) const { m_db->exec(QStringLiteral("UPDATE cliente_colecciones SET quitar_al_verse = ? WHERE id = ? AND servidor_id = ?"), {v ? 1 : 0, id, m_server}); }
void LocalState::deleteCollection(const QString &id) const { m_db->exec(QStringLiteral("DELETE FROM cliente_colecciones WHERE id = ? AND servidor_id = ?"), {id, m_server}); }

QVector<LocalState::Collection> LocalState::collectionsOfItem(const QString &itemId) const
{
    QVector<Collection> out;
    for (const QVariantMap &r : m_db->rows(QStringLiteral("SELECT c.* FROM cliente_colecciones c JOIN cliente_coleccion_items ci ON ci.coleccion_id = c.id WHERE ci.media_item_id = ? AND c.servidor_id = ? ORDER BY c.nombre COLLATE NOCASE"), {itemId, m_server})) out.append(toCollection(r));
    return out;
}

QStringList LocalState::collectionItems(const QString &cid) const
{
    QStringList out;
    for (const QVariantMap &r : m_db->rows(QStringLiteral("SELECT media_item_id AS id FROM cliente_coleccion_items WHERE coleccion_id = ? ORDER BY agregado_en DESC, id DESC"), {cid})) out << r["id"].toString();
    return out;
}

void LocalState::addToCollection(const QString &cid, const QString &itemId) const { m_db->exec(QStringLiteral("INSERT OR IGNORE INTO cliente_coleccion_items (coleccion_id, media_item_id, agregado_en) VALUES (?, ?, ?)"), {cid, itemId, now()}); }
void LocalState::removeFromCollection(const QString &cid, const QString &itemId) const { m_db->exec(QStringLiteral("DELETE FROM cliente_coleccion_items WHERE coleccion_id = ? AND media_item_id = ?"), {cid, itemId}); }

LocalProgress LocalState::collectionProgress(const QString &cid, const QString &itemId) const
{
    return toProgress(m_db->row(QStringLiteral("SELECT * FROM cliente_coleccion_progresos WHERE coleccion_id = ? AND media_item_id = ?"), {cid, itemId}));
}

void LocalState::saveCollectionProgress(const QString &cid, const QString &itemId, qint64 pos, const QString &state, const QVariant &audio, const QVariant &sub) const
{
    m_db->exec(QStringLiteral("INSERT INTO cliente_coleccion_progresos (coleccion_id, media_item_id, posicion_segundos, estado, ultima_reproduccion, pista_audio_id, pista_subtitulo_id) VALUES (?, ?, ?, ?, ?, ?, ?)"
                             " ON CONFLICT (coleccion_id, media_item_id) DO UPDATE SET posicion_segundos = excluded.posicion_segundos, estado = excluded.estado, ultima_reproduccion = excluded.ultima_reproduccion,"
                             " pista_audio_id = COALESCE(excluded.pista_audio_id, pista_audio_id), pista_subtitulo_id = COALESCE(excluded.pista_subtitulo_id, pista_subtitulo_id)"),
              {cid, itemId, pos, state, now(), audio.isNull() ? nullVar() : audio, sub.isNull() ? nullVar() : sub});
}

void LocalState::markSeenInCollection(const QString &cid, const QString &itemId) const
{
    m_db->begin();
    saveCollectionProgress(cid, itemId, 0, QStringLiteral("visto"));
    if (collection(cid).removeWhenSeen) removeFromCollection(cid, itemId);
    m_db->commit();
}

void LocalState::markUnseenInCollection(const QString &cid, const QString &itemId) const { m_db->exec(QStringLiteral("DELETE FROM cliente_coleccion_progresos WHERE coleccion_id = ? AND media_item_id = ?"), {cid, itemId}); }

QVector<QPair<QString, qint64>> LocalState::continueInCollection(const QString &cid) const
{
    QVector<QPair<QString, qint64>> out;
    for (const QVariantMap &r : m_db->rows(QStringLiteral("SELECT media_item_id AS id, posicion_segundos AS pos FROM cliente_coleccion_progresos WHERE coleccion_id = ? AND estado = 'enProgreso' ORDER BY ultima_reproduccion DESC"), {cid}))
        out.append({r["id"].toString(), r["pos"].toLongLong()});
    return out;
}

QMap<QString, LocalProgress> LocalState::collectionProgresses(const QString &cid) const
{
    QMap<QString, LocalProgress> out;
    for (const QVariantMap &r : m_db->rows(QStringLiteral("SELECT * FROM cliente_coleccion_progresos WHERE coleccion_id = ?"), {cid})) out.insert(r["media_item_id"].toString(), toProgress(r));
    return out;
}
