#include "Activity.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>

#include "HttpServer.h"
#include "Store.h"

namespace {
struct Entry { QString who, title, client; qint64 since = 0, last = 0; };
QMutex g_mutex;
QHash<QString, Entry> g_entries;

QString clientOf(const HttpRequest &r)
{
    const QString ua = r.headers.value(QStringLiteral("user-agent"));
    QString app;
    if (ua.contains(QLatin1String("Electron/")) || ua.contains(QLatin1String("CinemaLove"))) app = QStringLiteral("CinemaLove");
    else if (ua.contains(QLatin1String("Dart"))) app = QStringLiteral("CinemaLove (Android)");
    else app = ua.section(QLatin1Char(' '), 0, 0).left(30);
    if (app.isEmpty()) app = QStringLiteral("Cliente");
    return app + QStringLiteral(" · ") + r.peer;
}
}

namespace Activity {

void mark(const HttpRequest &r, const QString &kind, const QString &id)
{
    const QString who = r.principal == QLatin1String("admin") ? QStringLiteral("Administrador (este equipo)")
                        : r.principal == QLatin1String("token") ? QStringLiteral("Token de acceso")
                        : (r.userName.isEmpty() ? QStringLiteral("Usuario") : r.userName);
    const QVariantMap row = Store::row(kind == QLatin1String("item") ? QStringLiteral("SELECT titulo, ruta_relativa FROM media_items WHERE id = ?")
                                                                     : QStringLiteral("SELECT titulo, ruta_relativa FROM extras WHERE id = ?"), {id});
    QString title = row.value(QStringLiteral("titulo")).toString();
    if (title.isEmpty()) title = row.value(QStringLiteral("ruta_relativa")).toString();
    if (title.isEmpty()) title = id;
    const QString key = r.principal + QLatin1Char(':') + r.userId + QLatin1Char('|') + kind + QLatin1Char(':') + id;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QMutexLocker lock(&g_mutex);
    Entry &e = g_entries[key];
    if (e.since == 0) e.since = now;
    e.who = who; e.title = title; e.client = clientOf(r); e.last = now;
}

QJsonArray active(int windowMs)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QList<Entry> list;
    {
        QMutexLocker lock(&g_mutex);
        for (auto it = g_entries.begin(); it != g_entries.end();) {
            if (it->last < now - windowMs) it = g_entries.erase(it);
            else { list << *it; ++it; }
        }
    }
    std::sort(list.begin(), list.end(), [](const Entry &a, const Entry &b) { return a.since < b.since; });
    QJsonArray out;
    for (const Entry &e : list) out.append(QJsonObject{{"quien", e.who}, {"titulo", e.title}, {"cliente", e.client}, {"desde", double(e.since)}, {"ultimaVez", double(e.last)}});
    return out;
}

}
