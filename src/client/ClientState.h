#pragma once
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <optional>

#include "ClientDb.h"

/** Servidor remoto recordado en `cliente_datos.sqlite`. */
struct SavedServer {
    QString id, host;
    int port = 0;
    bool https = false;
    QString credentialType;        // sesion | apiKey
    QString apiKey, lastUser, sessionToken;
    qint64 sessionExpires = 0;     // 0 = ninguna
    bool trustCert = false;
    QString name, serverUuid, fingerprint, certPem;
    bool valid() const { return !id.isEmpty(); }
};

class Servers
{
public:
    explicit Servers(const ClientDb &db) : m_db(db) {}
    QVector<SavedServer> list() const;
    SavedServer get(const QString &id) const;
    SavedServer byAddress(const QString &host, int port) const;
    /** Crea o actualiza por (host, puerto). Solo cambian las claves presentes en `data`; un valor nulo borra. Claves: apiKey, credentialType,
     *  lastUser, sessionToken, sessionExpires, trustCert, name, serverUuid, fingerprint, certPem. */
    SavedServer save(const QString &host, int port, bool https, const QVariantMap &data);
    void remove(const QString &id) const;
    void forgetSession(const QString &id) const;

private:
    const ClientDb &m_db;
};

struct LocalProgress {
    qint64 position = 0;
    QString state;
    QString audio, sub;
    bool exists = false;
};

/** Progreso, «Próximas a ver» y colecciones de ESTE equipo para un servidor (Modo Cliente con token). Misma semántica que el servidor. */
class LocalState
{
public:
    LocalState(const ClientDb &db, const QString &serverId) : m_db(&db), m_server(serverId) {}

    LocalProgress progress(const QString &itemId) const;
    void saveProgress(const QString &itemId, qint64 pos, const QString &state, const QVariant &audio = {}, const QVariant &sub = {}) const;
    void markSeen(const QString &itemId) const;
    void markUnseen(const QString &itemId) const;
    void markMany(const QStringList &ids, const QString &state) const;
    QMap<QString, QPair<QString, qint64>> states() const;   // todo lo que no es «no visto»
    struct Row { QString id, state; qint64 position; QVariant last; };
    QVector<Row> rows() const;

    bool inQueue(const QString &itemId) const;
    void addToQueue(const QString &itemId) const;
    void removeFromQueue(const QString &itemId) const;
    QStringList queue(int limit = 500) const;

    struct Collection { QString id, name; bool removeWhenSeen = false; qint64 createdAt = 0; bool valid() const { return !id.isEmpty(); } };
    QVector<Collection> collections() const;
    Collection collection(const QString &id) const;
    QString createCollection(const QString &name, bool removeWhenSeen) const;
    void renameCollection(const QString &id, const QString &name) const;
    void setRemoveWhenSeen(const QString &id, bool v) const;
    void deleteCollection(const QString &id) const;
    QVector<Collection> collectionsOfItem(const QString &itemId) const;
    QStringList collectionItems(const QString &collectionId) const;
    void addToCollection(const QString &collectionId, const QString &itemId) const;
    void removeFromCollection(const QString &collectionId, const QString &itemId) const;

    LocalProgress collectionProgress(const QString &cid, const QString &itemId) const;
    void saveCollectionProgress(const QString &cid, const QString &itemId, qint64 pos, const QString &state, const QVariant &audio = {}, const QVariant &sub = {}) const;
    void markSeenInCollection(const QString &cid, const QString &itemId) const;
    void markUnseenInCollection(const QString &cid, const QString &itemId) const;
    QVector<QPair<QString, qint64>> continueInCollection(const QString &cid) const;
    QMap<QString, LocalProgress> collectionProgresses(const QString &cid) const;

private:
    const ClientDb *m_db;   // puntero: LocalState se copia (opcional dentro de las opciones del proxy)
    QString m_server;
};
