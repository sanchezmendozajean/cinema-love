#include "ImageApi.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

#include "HttpServer.h"
#include "Store.h"

namespace {

const QString kTmdbOrigin = QStringLiteral("https://image.tmdb.org/");

const QHash<QString, QString> kTypes = {{QStringLiteral(".jpg"), QStringLiteral("image/jpeg")}, {QStringLiteral(".jpeg"), QStringLiteral("image/jpeg")},
                                         {QStringLiteral(".png"), QStringLiteral("image/png")}, {QStringLiteral(".webp"), QStringLiteral("image/webp")}};

bool isTmdbUrl(const QString &u) { return u.startsWith(kTmdbOrigin) && !u.contains(QLatin1String("..")); }

QString extOf(const QString &url)
{
    const QString path = QUrl(url).path();
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return dot > slash && dot >= 0 ? path.mid(dot).toLower() : QStringLiteral(".jpg");
}

QNetworkAccessManager &nam()
{
    static thread_local QNetworkAccessManager *n = nullptr;
    if (!n) {
        n = new QNetworkAccessManager();
        n->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    }
    return *n;
}

QString imagesDir() { return Store::folder() + QStringLiteral("/images"); }

/** Índice de solo lectura de images/imageCacheInfo.json (flutter_cache_manager): imágenes que ya tiene el usuario. Nunca se escribe. */
const QHash<QString, QString> &flutterIndex()
{
    static thread_local QHash<QString, QString> map;
    static thread_local QString loadedFor;
    if (loadedFor == Store::folder()) return map;
    loadedFor = Store::folder();
    map.clear();
    QFile f(imagesDir() + QStringLiteral("/imageCacheInfo.json"));
    if (f.open(QIODevice::ReadOnly))
        for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array()) {
            const QJsonObject o = v.toObject();
            const QString url = o.value(QStringLiteral("url")).isString() ? o.value(QStringLiteral("url")).toString() : o.value(QStringLiteral("key")).toString();
            if (!url.isEmpty() && o.value(QStringLiteral("relativePath")).isString()) map.insert(url, o.value(QStringLiteral("relativePath")).toString());
        }
    return map;
}

struct Resolved { QString path, type; };

QString clPath(const QString &url) { return imagesDir() + QStringLiteral("/cl/") + QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex()) + extOf(url); }

Resolved findLocal(const QString &url)
{
    const QString rel = flutterIndex().value(url);
    if (!rel.isEmpty()) {
        const QString abs = imagesDir() + QLatin1Char('/') + rel;
        if (QFileInfo::exists(abs)) return {abs, kTypes.value(QFileInfo(abs).suffix().prepend(QLatin1Char('.')).toLower(), QStringLiteral("image/jpeg"))};
    }
    const QVariantMap row = Store::row(QStringLiteral("SELECT ruta_relativa FROM cl_image_cache WHERE url = ?"), {url});
    if (!row.isEmpty()) {
        const QString abs = imagesDir() + QLatin1Char('/') + row["ruta_relativa"].toString();
        if (QFileInfo::exists(abs)) return {abs, kTypes.value(QFileInfo(abs).suffix().prepend(QLatin1Char('.')).toLower(), QStringLiteral("image/jpeg"))};
    }
    const QString cl = clPath(url);   // descargas previas: la ruta es determinística
    if (QFileInfo::exists(cl)) return {cl, kTypes.value(extOf(url), QStringLiteral("image/jpeg"))};
    return {};
}

QHash<QString, QList<std::function<void(Resolved)>>> &inFlight()
{
    static thread_local QHash<QString, QList<std::function<void(Resolved)>>> m;
    return m;
}

void download(const QString &url, std::function<void(Resolved)> cb)
{
    auto &waiting = inFlight();
    if (waiting.contains(url)) { waiting[url].append(std::move(cb)); return; }   // una sola descarga por URL
    waiting[url].append(std::move(cb));
    QNetworkRequest req{QUrl(url)};
    req.setTransferTimeout(15000);
    QNetworkReply *reply = nam().get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, url] {
        Resolved out;
        const QString type = kTypes.value(extOf(url));
        if (reply->error() == QNetworkReply::NoError && !type.isEmpty()) {
            const QByteArray data = reply->readAll();
            const QString path = clPath(url);
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size()) {
                f.close();
                out = {path, type};
                if (!Store::readOnly())   // base en solo lectura: la imagen queda en disco y se encuentra por su ruta
                    Store::exec(QStringLiteral("INSERT OR REPLACE INTO cl_image_cache (url, ruta_relativa, bytes, creado_en) VALUES (?, ?, ?, ?)"),
                                {url, QStringLiteral("cl/") + QFileInfo(path).fileName(), qint64(data.size()), QDateTime::currentSecsSinceEpoch()});
            }
        }
        reply->deleteLater();
        const auto callbacks = inFlight().take(url);
        for (const auto &c : callbacks) c(out);
    });
}

// ---------------------------------------------------------------- personas (TMDb)

struct PersonTmdb {
    bool ok = false;
    QJsonValue nombre, biografia, nacimiento, fallecimiento, lugar, foto, departamento;
};

QHash<qlonglong, PersonTmdb> &personCache()
{
    static thread_local QHash<qlonglong, PersonTmdb> c;
    return c;
}

const QHash<QString, QString> kDepartments = {{"Acting", "Actuación"}, {"Directing", "Dirección"}, {"Writing", "Guion"}, {"Production", "Producción"},
                                               {"Camera", "Fotografía"}, {"Sound", "Sonido"}, {"Editing", "Montaje"}};

/** Biografía desde TMDb: es-MX con fallback a en-US si la biografía viene vacía. Sin clave o sin red devuelve ok=false. */
void fetchPerson(qlonglong id, const QString &key, int langIdx, PersonTmdb prev, std::function<void(PersonTmdb)> cb)
{
    static const QStringList langs{QStringLiteral("es-MX"), QStringLiteral("en-US")};
    QNetworkRequest req{QUrl(QStringLiteral("https://api.themoviedb.org/3/person/%1?language=%2&api_key=%3").arg(id).arg(langs.at(langIdx), QString::fromLatin1(QUrl::toPercentEncoding(key))))};
    req.setTransferTimeout(10000);
    QNetworkReply *reply = nam().get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, id, key, langIdx, prev, cb] {
        const bool okNet = reply->error() == QNetworkReply::NoError;
        const QJsonObject d = okNet ? QJsonDocument::fromJson(reply->readAll()).object() : QJsonObject();
        reply->deleteLater();
        if (!okNet) { cb(prev); return; }   // sin red / inexistente: no se cachea para poder reintentar luego
        PersonTmdb r;
        r.ok = true;
        const QString bio = d.value(QStringLiteral("biography")).toString().trimmed();
        r.nombre = d.value(QStringLiteral("name")).isString() ? d.value(QStringLiteral("name")) : QJsonValue(QJsonValue::Null);
        r.biografia = !bio.isEmpty() ? QJsonValue(bio) : prev.biografia;
        r.nacimiento = d.value(QStringLiteral("birthday")).isString() ? d.value(QStringLiteral("birthday")) : QJsonValue(QJsonValue::Null);
        r.fallecimiento = d.value(QStringLiteral("deathday")).isString() ? d.value(QStringLiteral("deathday")) : QJsonValue(QJsonValue::Null);
        r.lugar = d.value(QStringLiteral("place_of_birth")).isString() ? d.value(QStringLiteral("place_of_birth")) : QJsonValue(QJsonValue::Null);
        const QString prof = d.value(QStringLiteral("profile_path")).toString();
        r.foto = prof.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(QStringLiteral("https://image.tmdb.org/t/p/w500") + prof);
        const QString dep = d.value(QStringLiteral("known_for_department")).toString();
        r.departamento = dep.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(kDepartments.value(dep, dep));
        if (!r.biografia.isNull() && !r.biografia.toString().isEmpty()) { cb(r); return; }
        if (langIdx + 1 < langs.size()) fetchPerson(id, key, langIdx + 1, r, cb);
        else cb(r);
    });
}

} // namespace

void registerImageRoutes(HttpServer &h)
{
    h.routeAsync("GET", "/api/v1/image", [](HttpRequest &r, std::function<void(HttpResponse)> done) {
        const QString src = r.q(QStringLiteral("src")), size = r.q(QStringLiteral("size"));
        if (src.isEmpty() || !isTmdbUrl(src)) { done(HttpResponse::error(400, QStringLiteral("Solo se aceptan imágenes de image.tmdb.org."))); return; }
        static const QRegularExpression sizeRe(QStringLiteral("^(w\\d{2,4}|original)$"));
        if (r.hasQ(QStringLiteral("size")) && !sizeRe.match(size).hasMatch()) { done(HttpResponse::error(400, QStringLiteral("Tamaño inválido."))); return; }
        QString url = src;
        if (!size.isEmpty()) url.replace(QRegularExpression(QStringLiteral("/t/p/(w\\d+|original)/")), QStringLiteral("/t/p/") + size + QStringLiteral("/"));
        auto reply = [done](const Resolved &img) {
            if (img.path.isEmpty()) { done(HttpResponse::error(404, QStringLiteral("Imagen no disponible."))); return; }
            HttpResponse res = HttpResponse::file(img.path, img.type);
            res.header("Cache-Control", "public, max-age=31536000, immutable");
            done(res);
        };
        const Resolved local = findLocal(url);
        if (!local.path.isEmpty()) { reply(local); return; }
        download(url, reply);
    });

    h.routeAsync("GET", "/api/v1/people/:id", [](HttpRequest &r, std::function<void(HttpResponse)> done) {
        bool ok = false;
        const qlonglong id = r.params["id"].toLongLong(&ok);
        if (!ok) { done(HttpResponse::error(400, QStringLiteral("El id de persona debe ser numérico."))); return; }
        // Datos del catálogo: nombre y foto del primero que aparezca (reparto antes que equipo)
        const QVariantMap local = Store::row(QStringLiteral(
            "SELECT json_extract(j.value, '$.nombre') AS nombre, json_extract(j.value, '$.fotoUrl') AS foto, j.es_equipo AS equipo FROM ("
            " SELECT je.value, 0 AS es_equipo FROM media_items m, json_each(CASE WHEN json_valid(m.reparto) THEN m.reparto ELSE '[]' END) je"
            " UNION ALL SELECT je.value, 0 FROM series s, json_each(CASE WHEN json_valid(s.reparto) THEN s.reparto ELSE '[]' END) je"
            " UNION ALL SELECT je.value, 1 FROM media_items m, json_each(CASE WHEN json_valid(m.equipo) THEN m.equipo ELSE '[]' END) je"
            " UNION ALL SELECT je.value, 1 FROM series s, json_each(CASE WHEN json_valid(s.equipo) THEN s.equipo ELSE '[]' END) je"
            ") j WHERE json_extract(j.value, '$.id') = ? ORDER BY json_extract(j.value, '$.fotoUrl') IS NULL, j.es_equipo LIMIT 1"), {id});
        const bool hasLocal = !local.isEmpty() && !local["nombre"].toString().isEmpty();
        auto build = [done, id, local, hasLocal](const PersonTmdb &t) {
            if (!hasLocal && !t.ok) { done(HttpResponse::error(404, QStringLiteral("Persona no encontrada."))); return; }
            auto pick = [](const QJsonValue &a, const QJsonValue &b) { return a.isNull() || a.isUndefined() ? b : a; };
            const QJsonValue localName = hasLocal ? QJsonValue(local["nombre"].toString()) : QJsonValue(QString());
            const QJsonValue localPhoto = hasLocal && !local["foto"].isNull() ? QJsonValue(local["foto"].toString()) : QJsonValue(QJsonValue::Null);
            const QJsonValue dep = t.ok && !t.departamento.isNull() ? t.departamento : (hasLocal ? QJsonValue(local["equipo"].toInt() == 1 ? QStringLiteral("Equipo") : QStringLiteral("Actuación")) : QJsonValue(QJsonValue::Null));
            done(HttpResponse::json(QJsonObject{{"id", id}, {"nombre", t.ok ? pick(t.nombre, localName) : localName}, {"fotoUrl", t.ok ? pick(t.foto, localPhoto) : localPhoto},
                                                {"departamento", dep}, {"biografia", t.ok ? t.biografia : QJsonValue(QJsonValue::Null)},
                                                {"nacimiento", t.ok ? t.nacimiento : QJsonValue(QJsonValue::Null)}, {"fallecimiento", t.ok ? t.fallecimiento : QJsonValue(QJsonValue::Null)},
                                                {"lugarNacimiento", t.ok ? t.lugar : QJsonValue(QJsonValue::Null)}}));
        };
        const auto cached = personCache().constFind(id);
        if (cached != personCache().constEnd()) { build(cached.value()); return; }
        const QString key = Store::scalar(QStringLiteral("SELECT valor FROM app_settings WHERE clave = 'tmdb_api_key'")).toString();
        if (key.isEmpty()) { build(PersonTmdb()); return; }
        fetchPerson(id, key, 0, PersonTmdb(), [id, build](PersonTmdb t) {
            if (t.ok) personCache().insert(id, t);
            build(t);
        });
    });
}
