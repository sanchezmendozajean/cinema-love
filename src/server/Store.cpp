#include "Store.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QDateTime>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <csignal>
#include <cerrno>
#endif

namespace {

QMutex g_mutex;
QString g_folder;
bool g_readOnly = false;
QStringList g_lockFiles;

constexpr int kSchemaVersion = 11;
const QStringList kLocks = {QStringLiteral("cinemalove.electron.lock"), QStringLiteral("cinemalove.qt.lock")};
const QString kBackupPrefix = QStringLiteral("cinemalove.sqlite.bak-qt-");

bool processAlive(qint64 pid)
{
#ifdef Q_OS_WIN
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!h) return GetLastError() == ERROR_ACCESS_DENIED;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
#else
    return kill(pid_t(pid), 0) == 0 || errno == EPERM;
#endif
}

QString readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QString connName() { return QStringLiteral("cl-store-%1").arg(quintptr(QThread::currentThreadId())); }

bool takeLocks(const QString &dir, QString *error)
{
    const qint64 me = QCoreApplication::applicationPid();
    for (const QString &name : kLocks) {
        const QString p = dir + QLatin1Char('/') + name;
        if (!QFileInfo::exists(p)) continue;
        bool ok = false;
        const qint64 pid = readFile(p).trimmed().toLongLong(&ok);
        if (ok && pid != me && processAlive(pid)) {
            *error = QStringLiteral("Otra instancia de CinemaLove (o una de sus otras versiones) tiene abiertos estos datos. Ciérrala e inténtalo de nuevo.");
            return false;
        }
    }
    for (const QString &name : kLocks) {
        QFile f(dir + QLatin1Char('/') + name);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QByteArray::number(me));
    }
    return true;
}

void releaseLocks(const QString &dir)
{
    const qint64 me = QCoreApplication::applicationPid();
    for (const QString &name : kLocks) {
        const QString p = dir + QLatin1Char('/') + name;
        if (QFileInfo::exists(p) && readFile(p).trimmed().toLongLong() == me) QFile::remove(p);
    }
}

/** Copia consistente (VACUUM INTO) de la base cada 7 días; conserva los 5 últimos. Nunca toca la original. */
QString backupIfNeeded(const QString &dir, const QString &dbPath)
{
    QDir d(dir);
    QStringList prev = d.entryList({kBackupPrefix + QLatin1Char('*')}, QDir::Files, QDir::Name);
    if (!prev.isEmpty()) {
        const double ageDays = QFileInfo(dir + QLatin1Char('/') + prev.last()).lastModified().secsTo(QDateTime::currentDateTime()) / 86400.0;
        if (ageDays < 7) return {};
    }
    QString name = kBackupPrefix + QDate::currentDate().toString(QStringLiteral("yyyyMMdd"));
    if (QFileInfo::exists(dir + QLatin1Char('/') + name)) name += QStringLiteral("-%1").arg(QDateTime::currentMSecsSinceEpoch() % 100000);
    const QString dest = dir + QLatin1Char('/') + name;
    bool done = false;
    {
        const QString cn = QStringLiteral("cl-backup-%1").arg(quintptr(QThread::currentThreadId()));
        {
            QSqlDatabase src = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), cn);
            src.setDatabaseName(dbPath);
            src.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            if (src.open()) {
                QSqlQuery q(src);
                done = q.exec(QStringLiteral("VACUUM INTO '%1'").arg(QString(dest).replace(QLatin1Char('\''), QStringLiteral("''"))));
            }
            src.close();
        }
        QSqlDatabase::removeDatabase(cn);
    }
    if (!done) {
        QFile::remove(dest);
        QFile::copy(dbPath, dest);
    }
    prev = d.entryList({kBackupPrefix + QLatin1Char('*')}, QDir::Files, QDir::Name);
    while (prev.size() > 5) QFile::remove(dir + QLatin1Char('/') + prev.takeFirst());
    return name;
}

const char *kMigrations[] = {
    // 1
    "CREATE TABLE IF NOT EXISTS cl_meta (clave TEXT PRIMARY KEY, valor TEXT NOT NULL);"
    "CREATE TABLE IF NOT EXISTS cl_media_probe (objetivo_tipo TEXT NOT NULL, objetivo_id TEXT NOT NULL, tamano_bytes INTEGER NOT NULL, mtime_ms INTEGER NOT NULL, probe_json TEXT NOT NULL, creado_en INTEGER NOT NULL, PRIMARY KEY (objetivo_tipo, objetivo_id));"
    "CREATE TABLE IF NOT EXISTS cl_image_cache (url TEXT PRIMARY KEY, ruta_relativa TEXT NOT NULL, bytes INTEGER NOT NULL, creado_en INTEGER NOT NULL);"
    "CREATE TABLE IF NOT EXISTS cl_user_prefs (usuario_id TEXT, clave TEXT NOT NULL, valor TEXT NOT NULL, UNIQUE (usuario_id, clave));"
    "CREATE INDEX IF NOT EXISTS cl_idx_media_items_catalogo ON media_items (library_id, series_id, estado_archivo);"
    "CREATE INDEX IF NOT EXISTS cl_idx_watch_progresses_item ON watch_progresses (media_item_id, usuario_id);"
    "CREATE INDEX IF NOT EXISTS cl_idx_proximas_item ON proximas_a_ver (media_item_id, usuario_id);",
    // 2
    "CREATE INDEX IF NOT EXISTS cl_idx_media_items_serie ON media_items (series_id, estado_archivo, numero_temporada, numero_episodio);"
    "CREATE INDEX IF NOT EXISTS cl_idx_series_catalogo ON series (library_id, estado_archivo);"};

void migrate(QSqlDatabase db)
{
    QSqlQuery q(db);
    int current = 0;
    if (q.exec(QStringLiteral("SELECT valor FROM cl_meta WHERE clave = 'esquema'")) && q.next()) current = q.value(0).toInt();
    const int latest = int(sizeof(kMigrations) / sizeof(kMigrations[0]));
    for (int v = current + 1; v <= latest; ++v) {
        db.transaction();
        for (const QString &stmt : QString::fromUtf8(kMigrations[v - 1]).split(QLatin1Char(';'), Qt::SkipEmptyParts)) q.exec(stmt);
        q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS cl_meta (clave TEXT PRIMARY KEY, valor TEXT NOT NULL)"));
        q.prepare(QStringLiteral("INSERT INTO cl_meta (clave, valor) VALUES ('esquema', ?) ON CONFLICT(clave) DO UPDATE SET valor = excluded.valor"));
        q.addBindValue(QString::number(v));
        q.exec();
        db.commit();
    }
}

} // namespace

QString Store::folder() { return g_folder; }
QString Store::dbFile() { return g_folder + QStringLiteral("/cinemalove.sqlite"); }
bool Store::readOnly() { return g_readOnly; }

Store::OpenResult Store::open(const QString &folder)
{
    OpenResult r;
    close();
    const QString dir = QDir(folder).absolutePath();
    const QString path = dir + QStringLiteral("/cinemalove.sqlite");
    const bool existed = QFileInfo::exists(path);
    QDir().mkpath(dir);
    QString lockErr;
    if (!takeLocks(dir, &lockErr)) { r.code = QStringLiteral("bloqueada"); r.message = lockErr; return r; }
    if (existed) {
        // Versión del esquema: se mira en solo lectura antes de decidir cómo abrir.
        int v = 0;
        {
            const QString cn = QStringLiteral("cl-probe");
            {
                QSqlDatabase p = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), cn);
                p.setDatabaseName(path);
                p.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
                if (p.open()) { QSqlQuery q(p); if (q.exec(QStringLiteral("PRAGMA user_version")) && q.next()) v = q.value(0).toInt(); }
                p.close();
            }
            QSqlDatabase::removeDatabase(cn);
        }
        if (v < kSchemaVersion) {
            releaseLocks(dir);
            r.code = QStringLiteral("version-antigua");
            r.message = QStringLiteral("Esta base fue creada con una versión antigua de CinemaLove. Ábrela una vez con la versión Flutter para actualizarla.");
            return r;
        }
        if (v > kSchemaVersion) {
            g_readOnly = true;
            r.readOnly = true;
            r.warning = QStringLiteral("Esta base es de una versión más nueva de CinemaLove. Se abrió en solo lectura para no arriesgar tus datos.");
        }
        r.backup = backupIfNeeded(dir, path);
    }
    g_folder = dir;
    QSqlDatabase db = Store::db();   // crea la conexión de este hilo
    if (!db.isOpen()) {
        r.code = QStringLiteral("error");
        r.message = db.lastError().text();
        releaseLocks(dir);
        g_folder.clear();
        return r;
    }
    if (!existed) {
        QFile schema(QStringLiteral(":/server/schema-v11.sql"));
        QSqlQuery q(db);
        if (schema.open(QIODevice::ReadOnly)) {
            db.transaction();
            for (const QString &stmt : QString::fromUtf8(schema.readAll()).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
                const QString s = stmt.trimmed();
                if (!s.isEmpty() && !s.startsWith(QLatin1String("--"))) q.exec(s);
            }
            q.exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion));
            db.commit();
        }
        r.created = true;
    }
    if (!g_readOnly) migrate(db);
    r.ok = true;
    return r;
}

void Store::close()
{
    if (g_folder.isEmpty()) return;
    closeThreadConnection();
    releaseLocks(g_folder);
    g_folder.clear();
    g_readOnly = false;
}

QSqlDatabase Store::db()
{
    const QString cn = connName();
    if (QSqlDatabase::contains(cn)) return QSqlDatabase::database(cn);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), cn);
    db.setDatabaseName(dbFile());
    db.setConnectOptions(g_readOnly ? QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000") : QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (db.open()) {
        QSqlQuery q(db);
        q.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    }
    return db;
}

void Store::closeThreadConnection()
{
    const QString cn = connName();
    if (!QSqlDatabase::contains(cn)) return;
    { QSqlDatabase d = QSqlDatabase::database(cn); d.close(); }
    QSqlDatabase::removeDatabase(cn);
}

QVector<QVariantMap> Store::rows(const QString &sql, const QVariantList &binds)
{
    QVector<QVariantMap> out;
    QSqlQuery q(db());
    q.setForwardOnly(true);
    q.prepare(sql);
    for (const QVariant &b : binds) q.addBindValue(b);
    if (!q.exec()) { qWarning("sql: %s (%s)", qPrintable(q.lastError().text()), qPrintable(sql.left(80))); return out; }
    const QSqlRecord rec = q.record();
    while (q.next()) {
        QVariantMap m;
        for (int i = 0; i < rec.count(); ++i) m.insert(rec.fieldName(i), q.value(i));
        out.append(m);
    }
    return out;
}

QVariantMap Store::row(const QString &sql, const QVariantList &binds)
{
    const auto r = rows(sql, binds);
    return r.isEmpty() ? QVariantMap() : r.first();
}

QVariant Store::scalar(const QString &sql, const QVariantList &binds)
{
    QSqlQuery q(db());
    q.prepare(sql);
    for (const QVariant &b : binds) q.addBindValue(b);
    return q.exec() && q.next() ? q.value(0) : QVariant();
}

bool Store::exec(const QString &sql, const QVariantList &binds, QString *error)
{
    QSqlQuery q(db());
    q.prepare(sql);
    for (const QVariant &b : binds) q.addBindValue(b);
    const bool ok = q.exec();
    if (!ok && error) *error = q.lastError().text();
    return ok;
}
