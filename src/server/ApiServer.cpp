#include "ApiServer.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QDir>

#include "Catalog.h"
#include "Mappers.h"
#include "HttpServer.h"
#include "Store.h"
#include "UserApi.h"

using namespace Mappers;

namespace {

const QHash<QString, QString> kMime = {
    {"mp4", "video/mp4"}, {"m4v", "video/x-m4v"}, {"mkv", "video/x-matroska"}, {"avi", "video/x-msvideo"}, {"mov", "video/quicktime"},
    {"wmv", "video/x-ms-wmv"}, {"flv", "video/x-flv"}, {"webm", "video/webm"}, {"ts", "video/mp2t"}, {"mpg", "video/mpeg"}, {"mpeg", "video/mpeg"}};

/** Ruta física de un archivo de la biblioteca; vacía si se sale de su carpeta (path traversal). */
QString safePath(const QString &libRoot, const QString &rel)
{
    const QDir root(libRoot);
    const QString abs = QDir::cleanPath(root.absoluteFilePath(rel));
    const QString r = QDir::cleanPath(root.absolutePath());
    return abs.startsWith(r + QLatin1Char('/')) ? abs : QString();
}

HttpResponse serveVideo(const QString &libId, const QString &rel)
{
    const QVariantMap lib = Store::row(QStringLiteral("SELECT * FROM libraries WHERE id = ?"), {libId});
    if (lib.isEmpty()) return HttpResponse::error(404, QStringLiteral("Biblioteca no encontrada."));
    const QString path = safePath(lib["ruta_actual"].toString(), rel);
    if (path.isEmpty()) return HttpResponse::error(400, QStringLiteral("Ruta de archivo inválida."));
    if (!QFileInfo::exists(path)) return HttpResponse::error(404, QStringLiteral("El archivo no se encuentra en la ruta actual."));
    return HttpResponse::file(path, kMime.value(QFileInfo(path).suffix().toLower(), QStringLiteral("application/octet-stream")));
}

QString randomToken(int bytes)
{
    QByteArray b(bytes, 0);
    for (int i = 0; i < bytes; ++i) b[i] = char(QRandomGenerator::system()->bounded(256));
    return QString::fromLatin1(b.toHex());
}

bool constantTimeEq(const QString &a, const QString &b)
{
    const QByteArray x = a.toUtf8(), y = b.toUtf8();
    if (x.size() != y.size()) return false;
    int d = 0;
    for (int i = 0; i < x.size(); ++i) d |= x[i] ^ y[i];
    return d == 0;
}

} // namespace

ApiServer::ApiServer() = default;
ApiServer::~ApiServer() { stop(); }

QString ApiServer::baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_port); }

quint16 ApiServer::start(quint16 port)
{
    m_adminToken = randomToken(24);
    m_http = std::make_unique<HttpServer>();
    m_http->setThreadDone([] { Store::closeThreadConnection(); });
    registerRoutes();
    m_port = m_http->listen(QStringLiteral("127.0.0.1"), port);
    return m_port;
}

void ApiServer::stop()
{
    if (m_http) { m_http->stop(); m_http.reset(); }
}

void ApiServer::registerRoutes()
{
    HttpServer &h = *m_http;
    const QString token = m_adminToken;

    // Autenticación (specs/04 §2): token admin efímero, API key activa o token de sesión de un usuario.
    h.setGuard([token](HttpRequest &req, HttpResponse &res) {
        if (req.method == QLatin1String("OPTIONS")) return true;
        if (!req.path.startsWith(QLatin1String("/api/v1/"))) return true;
        if (req.path == QLatin1String("/api/v1/health") || req.path == QLatin1String("/api/v1/login")) return true;
        QString cred = req.headers.value(QStringLiteral("x-api-key"));
        if (cred.isEmpty()) cred = req.q(QStringLiteral("api_key"));
        if (!cred.isEmpty()) {
            if (constantTimeEq(cred, token)) { req.principal = QStringLiteral("admin"); return true; }
            const QVariant key = Store::scalar(QStringLiteral("SELECT valor FROM api_keys WHERE activa = 1 LIMIT 1"));
            if (key.isValid() && constantTimeEq(cred, key.toString())) { req.principal = QStringLiteral("token"); return true; }
            const QVariantMap s = Store::row(QStringLiteral("SELECT usuario_id, expira_en FROM sesiones WHERE token = ?"), {cred});
            if (!s.isEmpty()) {
                if (s["expira_en"].toLongLong() <= QDateTime::currentSecsSinceEpoch()) {
                    Store::exec(QStringLiteral("DELETE FROM sesiones WHERE token = ?"), {cred});
                } else {
                    const QVariantMap u = Store::row(QStringLiteral("SELECT id, nombre_usuario FROM usuarios WHERE id = ?"), {s["usuario_id"]});
                    if (!u.isEmpty()) { req.principal = QStringLiteral("usuario"); req.userId = u["id"].toString(); req.userName = u["nombre_usuario"].toString(); req.sessionToken = cred; return true; }
                }
            }
        }
        res = HttpResponse::error(401, QStringLiteral("Credencial inválida o faltante."));
        return false;
    });

    // --- sistema
    h.route("GET", "/api/v1/health", [](HttpRequest &) {
        const QVariant id = Store::scalar(QStringLiteral("SELECT valor FROM cl_meta WHERE clave = 'server_id'"));
        return HttpResponse::json(QJsonObject{{"status", "ok"}, {"app", "cinemalove"}, {"serverId", id.isValid() ? id.toString() : QStringLiteral("desconocido")},
                                              {"nombre", QSysInfo::machineHostName()}, {"apiVersion", 1}, {"plataforma", "qt"}});
    });
    h.route("GET", "/api/v1/me", [](HttpRequest &r) {
        QJsonArray caps{"browse", "play", "progress"};
        if (r.principal != QLatin1String("token")) caps.append(QStringLiteral("userLists"));
        if (r.principal == QLatin1String("admin")) caps.append(QStringLiteral("admin"));
        QJsonObject o{{"principal", r.principal}, {"capabilities", caps}};
        if (!r.userName.isEmpty()) o.insert(QStringLiteral("nombreUsuario"), r.userName);
        return HttpResponse::json(o);
    });
    h.route("POST", "/api/v1/logout", [](HttpRequest &r) {
        if (!r.sessionToken.isEmpty()) Store::exec(QStringLiteral("DELETE FROM sesiones WHERE token = ?"), {r.sessionToken});
        return HttpResponse::json(QJsonObject{{"ok", true}});
    });

    registerUserRoutes(h);

    // --- catálogo [v1-legado]
    h.route("GET", "/api/v1/libraries", [](HttpRequest &) {
        QHash<QString, QJsonObject> summary;
        for (const QVariantMap &f : Store::rows(QStringLiteral(
                 "SELECT library_id AS id, COUNT(*) AS conteo, SUM(titulo IS NULL) AS sin FROM ("
                 " SELECT library_id, titulo FROM media_items WHERE series_id IS NULL AND estado_archivo = 'ok'"
                 " UNION ALL SELECT library_id, titulo FROM series WHERE estado_archivo = 'ok') GROUP BY library_id"))) {
            QJsonArray backdrops;
            for (const QVariantMap &b : Store::rows(QStringLiteral(
                     "SELECT backdrop_url FROM ("
                     " SELECT backdrop_url, creado_en FROM media_items WHERE library_id = ? AND series_id IS NULL AND estado_archivo = 'ok' AND backdrop_url IS NOT NULL"
                     " UNION ALL SELECT backdrop_url, creado_en FROM series WHERE library_id = ? AND estado_archivo = 'ok' AND backdrop_url IS NOT NULL"
                     ") ORDER BY creado_en DESC LIMIT 3"), {f["id"], f["id"]}))
                backdrops.append(b["backdrop_url"].toString());
            summary.insert(f["id"].toString(), QJsonObject{{"conteo", f["conteo"].toLongLong()}, {"sinMetadatos", f["sin"].toLongLong()}, {"backdrops", backdrops}});
        }
        QJsonArray out;
        for (const QVariantMap &l : Store::rows(QStringLiteral("SELECT * FROM libraries ORDER BY nombre COLLATE NOCASE"))) {
            QJsonObject o{{"id", l["id"].toString()}, {"nombre", l["nombre"].toString()}, {"tipo", l["tipo"].toString()}, {"creadoEn", isoDate(l["creado_en"].toLongLong())}};
            const QJsonObject s = summary.value(l["id"].toString(), QJsonObject{{"conteo", 0}, {"sinMetadatos", 0}, {"backdrops", QJsonArray()}});
            for (auto it = s.begin(); it != s.end(); ++it) o.insert(it.key(), it.value());
            out.append(o);
        }
        return HttpResponse::json(out);
    });
    h.route("GET", "/api/v1/libraries/:id/items", [](HttpRequest &r) {
        const QVariantMap lib = Store::row(QStringLiteral("SELECT * FROM libraries WHERE id = ?"), {r.params["id"]});
        if (lib.isEmpty()) return HttpResponse::error(404, QStringLiteral("Biblioteca no encontrada."));
        if (lib["tipo"].toString() != QLatin1String("pelicula")) return HttpResponse::error(400, QStringLiteral("Esta biblioteca no es de tipo película."));
        QJsonArray out;
        for (const QVariantMap &m : Store::rows(QStringLiteral("SELECT * FROM media_items WHERE library_id = ? AND series_id IS NULL AND estado_archivo = 'ok' ORDER BY ruta_relativa"), {lib["id"]}))
            out.append(mediaItem(m, false));
        return HttpResponse::json(out);
    });
    h.route("GET", "/api/v1/libraries/:id/series", [](HttpRequest &r) {
        const QVariantMap lib = Store::row(QStringLiteral("SELECT * FROM libraries WHERE id = ?"), {r.params["id"]});
        if (lib.isEmpty()) return HttpResponse::error(404, QStringLiteral("Biblioteca no encontrada."));
        if (lib["tipo"].toString() != QLatin1String("serie")) return HttpResponse::error(400, QStringLiteral("Esta biblioteca no es de tipo serie."));
        QJsonArray out;
        for (const QVariantMap &s : Store::rows(QStringLiteral("SELECT * FROM series WHERE library_id = ? AND estado_archivo = 'ok' ORDER BY ruta_relativa"), {lib["id"]}))
            out.append(serie(s, false));
        return HttpResponse::json(out);
    });
    h.route("GET", "/api/v1/items/:id", [](HttpRequest &r) {
        const QVariantMap m = Store::row(QStringLiteral("SELECT * FROM media_items WHERE id = ?"), {r.params["id"]});
        return m.isEmpty() ? HttpResponse::error(404, QStringLiteral("Ítem no encontrado.")) : HttpResponse::json(mediaItem(m, true));
    });
    h.route("GET", "/api/v1/items/:id/extras", [](HttpRequest &r) {
        QJsonArray out;
        for (const QVariantMap &e : Store::rows(QStringLiteral("SELECT * FROM extras WHERE media_item_id = ? ORDER BY titulo COLLATE NOCASE"), {r.params["id"]}))
            out.append(QJsonObject{{"id", e["id"].toString()}, {"mediaItemId", e["media_item_id"].toString()}, {"titulo", nz(e["titulo"])}});
        return HttpResponse::json(out);
    });
    h.route("GET", "/api/v1/series/:id", [](HttpRequest &r) {
        const QVariantMap s = Store::row(QStringLiteral("SELECT * FROM series WHERE id = ?"), {r.params["id"]});
        return s.isEmpty() ? HttpResponse::error(404, QStringLiteral("Serie no encontrada.")) : HttpResponse::json(serie(s, true));
    });
    h.route("GET", "/api/v1/series/:id/seasons", [](HttpRequest &r) {
        QJsonArray out;
        for (const QVariantMap &t : Store::rows(QStringLiteral("SELECT * FROM seasons WHERE series_id = ? ORDER BY numero_temporada"), {r.params["id"]}))
            out.append(QJsonObject{{"id", t["id"].toString()}, {"seriesId", t["series_id"].toString()}, {"numeroTemporada", t["numero_temporada"].toLongLong()},
                                   {"titulo", nz(t["titulo"])}, {"sinopsis", nz(t["sinopsis"])}, {"posterUrl", nz(t["poster_url"])}});
        return HttpResponse::json(out);
    });
    h.route("GET", "/api/v1/series/:id/episodes", [](HttpRequest &r) {
        QString sql = QStringLiteral("SELECT * FROM media_items WHERE series_id = ? AND estado_archivo = 'ok'");
        QVariantList b{r.params["id"]};
        if (r.hasQ(QStringLiteral("temporada"))) {
            bool ok = false;
            const int t = r.q(QStringLiteral("temporada")).toInt(&ok);
            if (!ok || r.q(QStringLiteral("temporada")).trimmed().isEmpty()) return HttpResponse::json(QJsonArray());
            sql += QStringLiteral(" AND numero_temporada = ?");
            b << t;
        }
        sql += QStringLiteral(" ORDER BY numero_temporada, numero_episodio");
        QJsonArray out;
        for (const QVariantMap &e : Store::rows(sql, b)) {
            QJsonObject o = mediaItem(e, false);
            o.insert(QStringLiteral("sinopsis"), nz(e["sinopsis"]));
            out.append(o);
        }
        return HttpResponse::json(out);
    });
    h.route("GET", "/api/v1/media/:id/stream", [](HttpRequest &r) {
        const QVariantMap m = Store::row(QStringLiteral("SELECT library_id, ruta_relativa FROM media_items WHERE id = ?"), {r.params["id"]});
        return m.isEmpty() ? HttpResponse::error(404, QStringLiteral("Ítem no encontrado.")) : serveVideo(m["library_id"].toString(), m["ruta_relativa"].toString());
    });
    // --- catálogo unificado e Inicio (nuevos)
    h.route("GET", "/api/v1/catalog", [](HttpRequest &r) {
        QHash<QString, QString> q;
        for (const auto &kv : r.query.queryItems(QUrl::FullyDecoded)) q.insert(kv.first, kv.second);
        Catalog::Filters f;
        const QString err = Catalog::parseFilters(q, &f);
        if (!err.isEmpty()) return HttpResponse::error(400, err);
        return HttpResponse::json(Catalog::search(f, r.userId));
    });
    h.route("GET", "/api/v1/home", [](HttpRequest &r) { return HttpResponse::json(Catalog::home(r.userId)); });
    auto idsOf = [](const HttpRequest &r) {
        QStringList ids;
        for (const QString &i : r.q(QStringLiteral("ids")).split(QLatin1Char(','), Qt::SkipEmptyParts)) if (!ids.contains(i)) ids << i;
        return ids.mid(0, 200);
    };
    h.route("GET", "/api/v1/catalog/items", [idsOf](HttpRequest &r) { return HttpResponse::json(Catalog::elementsByIds(idsOf(r), r.userId, false)); });
    h.route("GET", "/api/v1/catalog/continuar", [idsOf](HttpRequest &r) { return HttpResponse::json(Catalog::continueSummaries(idsOf(r))); });

    h.route("GET", "/api/v1/extras/:id/stream", [](HttpRequest &r) {
        const QVariantMap e = Store::row(QStringLiteral("SELECT media_item_id, ruta_relativa FROM extras WHERE id = ?"), {r.params["id"]});
        if (e.isEmpty()) return HttpResponse::error(404, QStringLiteral("Extra no encontrado."));
        const QVariantMap m = Store::row(QStringLiteral("SELECT library_id FROM media_items WHERE id = ?"), {e["media_item_id"]});
        return m.isEmpty() ? HttpResponse::error(404, QStringLiteral("Película asociada no encontrada.")) : serveVideo(m["library_id"].toString(), e["ruta_relativa"].toString());
    });
}
