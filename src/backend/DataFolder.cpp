#include "DataFolder.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTextStream>
#include <QUrl>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <csignal>
#include <cerrno>
#endif

namespace {

bool processAlive(qint64 pid)
{
    if (pid <= 0) return false;
#ifdef Q_OS_WIN
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!h) return GetLastError() == ERROR_ACCESS_DENIED; // existe pero sin permiso
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
#else
    return kill(pid_t(pid), 0) == 0 || errno == EPERM;
#endif
}

bool dirWritable(const QString &dir)
{
    QTemporaryFile f(QDir(dir).absoluteFilePath(QStringLiteral(".cl-escritura-XXXXXX")));
    return f.open(); // se borra sola al destruirse
}

} // namespace

DataFolder::DataFolder(QObject *parent) : QObject(parent) { m_path = readConfigured(); }

QString DataFolder::dbFile() const { return m_path.isEmpty() ? QString() : QDir(m_path).absoluteFilePath(m_fileName); }

QString DataFolder::configDir() const
{
    // Portátil: junto al ejecutable si se puede escribir; si no, la carpeta de configuración del usuario.
    const QString app = QCoreApplication::applicationDirPath();
    if (dirWritable(app)) return app;
    const QString cfg = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(cfg);
    return cfg;
}

QString DataFolder::configFile() const { return QDir(configDir()).absoluteFilePath(QStringLiteral("config.yaml")); }

QString DataFolder::readConfigured() const
{
    QFile f(configFile());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (!line.startsWith(QLatin1String("database_directory:"))) continue;
        QString v = line.mid(18).trimmed();
        if (v.size() >= 2 && ((v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"'))) || (v.startsWith(QLatin1Char('\'')) && v.endsWith(QLatin1Char('\'')))))
            v = v.mid(1, v.size() - 2);
        v.replace(QStringLiteral("\\\\"), QStringLiteral("\\")); // escape de JSON/YAML entre comillas dobles
        return QDir::fromNativeSeparators(v).isEmpty() ? QString() : v;
    }
    return {};
}

void DataFolder::setOverride(const QString &dir, const QString &fileName)
{
    m_override = true;
    m_fileName = fileName;
    m_path = dir;
    emit pathChanged();
}

QString DataFolder::suggested() const
{
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir::toNativeSeparators(QDir(docs).absoluteFilePath(QStringLiteral("CinemaLoveData")));
}

QVariantMap DataFolder::inspect(const QString &dirIn) const
{
    const QString dir = QDir::fromNativeSeparators(dirIn.trimmed());
    QVariantMap r{{"exists", false}, {"writable", false}, {"hasCollection", false}, {"locked", false}, {"bibliotecas", 0},
                  {"peliculas", 0}, {"series", 0}, {"sizeMb", 0.0}, {"error", QString()}};
    const QFileInfo fi(dir);
    if (dir.isEmpty()) return r;
    if (!fi.exists()) {
        // Carpeta nueva: se podrá crear si el padre es escribible.
        r["writable"] = fi.dir().exists() && dirWritable(fi.dir().absolutePath());
        return r;
    }
    if (!fi.isDir()) { r["error"] = tr("La ruta no es una carpeta."); return r; }
    r["exists"] = true;
    r["writable"] = dirWritable(dir);

    // ¿En uso por otra instancia (de cualquier versión)? Mismos nombres de bloqueo que el backend.
    for (const QString &name : {QStringLiteral("cinemalove.electron.lock"), QStringLiteral("cinemalove.qt.lock")}) {
        QFile lf(QDir(dir).absoluteFilePath(name));
        if (!lf.open(QIODevice::ReadOnly)) continue;
        const qint64 pid = lf.readAll().trimmed().toLongLong();
        if (pid != QCoreApplication::applicationPid() && processAlive(pid)) r["locked"] = true;
    }

    const QString file = QDir(dir).absoluteFilePath(QStringLiteral("cinemalove.sqlite"));
    if (!QFileInfo::exists(file)) return r;
    r["sizeMb"] = QFileInfo(file).size() / 1048576.0;
    const QString conn = QStringLiteral("inspect-") + QUuid::createUuid().toString(QUuid::Id128);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(file);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            auto count = [&db](const QString &sql) { QSqlQuery q(db); return q.exec(sql) && q.next() ? q.value(0).toInt() : -1; };
            const int libs = count(QStringLiteral("SELECT COUNT(*) FROM libraries"));
            if (libs >= 0) {
                r["hasCollection"] = true;
                r["bibliotecas"] = libs;
                r["peliculas"] = count(QStringLiteral("SELECT COUNT(*) FROM media_items WHERE series_id IS NULL AND estado_archivo = 'ok'"));
                r["series"] = count(QStringLiteral("SELECT COUNT(*) FROM series WHERE estado_archivo = 'ok'"));
            } else {
                r["error"] = tr("El archivo cinemalove.sqlite no es una base de CinemaLove.");
            }
            db.close();
        } else {
            r["error"] = db.lastError().text();
        }
    }
    QSqlDatabase::removeDatabase(conn);
    return r;
}

bool DataFolder::apply(const QString &dirIn)
{
    const QString dir = QDir::cleanPath(QDir::fromNativeSeparators(dirIn.trimmed()));
    m_error.clear();
    if (dir.isEmpty()) { m_error = tr("Elige una carpeta."); return false; }
    if (!QDir().mkpath(dir)) { m_error = tr("No se pudo crear la carpeta."); return false; }
    if (!dirWritable(dir)) { m_error = tr("No hay permiso para escribir en esa carpeta."); return false; }
    QFile f(configFile());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) { m_error = tr("No se pudo guardar la configuración."); return false; }
    QString native = QDir::toNativeSeparators(dir);
    native.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    // Mismo formato que las otras versiones: solo la clave database_directory.
    f.write(QStringLiteral("database_directory: \"%1\"\n").arg(native).toUtf8());
    f.close();
    m_path = QDir::toNativeSeparators(dir);
    emit pathChanged();
    return true;
}

void DataFolder::forget()
{
    if (!m_override) QFile::remove(configFile()); // solo el puntero; los datos no se tocan
    m_path.clear();
    emit pathChanged();
}

QStringList DataFolder::backups() const
{
    QStringList out;
    if (m_path.isEmpty()) return out;
    const QFileInfoList list = QDir(m_path).entryInfoList({QStringLiteral("cinemalove.sqlite.bak-*")}, QDir::Files, QDir::Time);
    for (const QFileInfo &fi : list)
        out << QStringLiteral("%1  ·  %2 MB  ·  %3").arg(fi.fileName()).arg(fi.size() / 1048576.0, 0, 'f', 1).arg(fi.lastModified().toString(QStringLiteral("dd/MM/yyyy HH:mm")));
    return out;
}

void DataFolder::openInExplorer() const
{
    if (!m_path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(m_path));
}
