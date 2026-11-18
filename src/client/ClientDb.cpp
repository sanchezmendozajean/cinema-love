#include "ClientDb.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QThread>

namespace {
constexpr int kSchemaVersion = 6;
thread_local QHash<const ClientDb *, int> g_depth;
QString connName(const ClientDb *d) { return QStringLiteral("cl-cliente-%1-%2").arg(quintptr(d)).arg(quintptr(QThread::currentThreadId())); }
}

ClientDb::~ClientDb() { closeThreadConnection(); }

QString ClientDb::open()
{
    const bool existed = QFileInfo::exists(m_path);
    QSqlDatabase d = db();
    if (!d.isOpen()) return d.lastError().text();
    QSqlQuery q(d);
    if (!existed) {
        QFile schema(QStringLiteral(":/client/schema-cliente-v6.sql"));
        if (!schema.open(QIODevice::ReadOnly)) return QStringLiteral("No se encuentra el esquema de cliente_datos.sqlite.");
        d.transaction();
        for (const QString &stmt : QString::fromUtf8(schema.readAll()).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const QString s = stmt.trimmed();
            if (!s.isEmpty() && !s.startsWith(QLatin1String("--"))) q.exec(s);
        }
        q.exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion));
        d.commit();
    } else {
        int v = 0;
        if (q.exec(QStringLiteral("PRAGMA user_version")) && q.next()) v = q.value(0).toInt();
        if (v != kSchemaVersion) return QStringLiteral("cliente_datos.sqlite tiene una versión de esquema (%1) que esta versión no conoce.").arg(v);
    }
    // Aditivo y propio: lo que Flutter no guarda de cada servidor remoto.
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS cl_cliente_servidor_extra (servidor_id TEXT PRIMARY KEY REFERENCES cliente_servidores (id) ON DELETE CASCADE,"
                          " nombre_servidor TEXT, server_uuid TEXT, huella_cert_sha256 TEXT, cert_pem TEXT)"));
    return {};
}

QSqlDatabase ClientDb::db() const
{
    const QString cn = connName(this);
    if (QSqlDatabase::contains(cn)) return QSqlDatabase::database(cn);
    QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), cn);
    d.setDatabaseName(m_path);
    d.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (d.open()) { QSqlQuery q(d); q.exec(QStringLiteral("PRAGMA foreign_keys = ON")); }
    return d;
}

void ClientDb::closeThreadConnection() const
{
    const QString cn = connName(this);
    if (!QSqlDatabase::contains(cn)) return;
    { QSqlDatabase d = QSqlDatabase::database(cn); d.close(); }
    QSqlDatabase::removeDatabase(cn);
}

QVector<QVariantMap> ClientDb::rows(const QString &sql, const QVariantList &binds) const
{
    QVector<QVariantMap> out;
    QSqlQuery q(db());
    q.setForwardOnly(true);
    q.prepare(sql);
    for (const QVariant &b : binds) q.addBindValue(b);
    if (!q.exec()) { qWarning("cliente sql: %s (%s)", qPrintable(q.lastError().text()), qPrintable(sql.left(80))); return out; }
    const QSqlRecord rec = q.record();
    while (q.next()) {
        QVariantMap m;
        for (int i = 0; i < rec.count(); ++i) m.insert(rec.fieldName(i), q.value(i));
        out.append(m);
    }
    return out;
}

QVariantMap ClientDb::row(const QString &sql, const QVariantList &binds) const
{
    const auto r = rows(sql, binds);
    return r.isEmpty() ? QVariantMap() : r.first();
}

QVariant ClientDb::scalar(const QString &sql, const QVariantList &binds) const
{
    QSqlQuery q(db());
    q.prepare(sql);
    for (const QVariant &b : binds) q.addBindValue(b);
    return q.exec() && q.next() ? q.value(0) : QVariant();
}

bool ClientDb::exec(const QString &sql, const QVariantList &binds) const
{
    QSqlQuery q(db());
    q.prepare(sql);
    for (const QVariant &b : binds) q.addBindValue(b);
    const bool ok = q.exec();
    if (!ok) qWarning("cliente sql: %s (%s)", qPrintable(q.lastError().text()), qPrintable(sql.left(80)));
    return ok;
}

void ClientDb::begin() const { if (g_depth[this]++ == 0) db().transaction(); }
void ClientDb::commit() const { if (g_depth[this] > 0 && --g_depth[this] == 0) db().commit(); }
