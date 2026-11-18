#include "AdminApi.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUuid>

#include "Bcrypt.h"
#include "HttpServer.h"
#include "Jobs.h"
#include "Mappers.h"
#include "Metadata.h"
#include "Naming.h"
#include "Scanner.h"
#include "Store.h"

using namespace Mappers;

namespace {

const QString kReadOnly = QStringLiteral("La base de datos está en solo lectura (es de una versión más nueva de CinemaLove).");

/** Error de validación con el mensaje que ve la persona; `status` es el HTTP que le corresponde. */
struct AdminError : std::runtime_error {
    AdminError(const QString &m, int s = 400) : std::runtime_error(m.toStdString()), status(s) {}
    int status;
};

QVariant nullVar() { return QVariant(QMetaType::fromType<QString>()); }
qint64 now() { return QDateTime::currentSecsSinceEpoch(); }

QJsonObject bodyObj(const HttpRequest &r)
{
    bool ok = false;
    const QJsonValue v = r.json(&ok);
    return ok && v.isObject() ? v.toObject() : QJsonObject();
}

bool isDir(const QString &p) { return QFileInfo(p).isDir(); }

QString resolvePath(const QString &p)
{
    QString abs = QDir::cleanPath(QFileInfo(p).absoluteFilePath());
    if (p.size() >= 2 && p.at(1) == QLatin1Char(':') && abs.size() >= 2 && abs.at(1) == QLatin1Char(':')) abs[0] = p.at(0);   // conserva la letra de unidad tal como se escribió
    return QDir::toNativeSeparators(abs);
}

struct Library { QString id, name, type, path; };

Library requireLibrary(const QString &id)
{
    const QVariantMap b = Store::row(QStringLiteral("SELECT id, nombre, tipo, ruta_actual FROM libraries WHERE id = ?"), {id});
    if (b.isEmpty()) throw AdminError(QStringLiteral("La biblioteca no existe."), 404);
    return {b["id"].toString(), b["nombre"].toString(), b["tipo"].toString(), b["ruta_actual"].toString()};
}

QString maskKey(const QString &k) { return k.isEmpty() ? QString() : QString(8, QChar(0x2022)) + k.right(4); }
QJsonValue maskJson(const QString &k) { return k.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(maskKey(k)); }

QString tmdbKey()
{
    const QString k = Store::scalar(QStringLiteral("SELECT valor FROM app_settings WHERE clave = 'tmdb_api_key'")).toString().trimmed();
    if (k.isEmpty()) throw AdminError(QStringLiteral("Configura tu API key de TMDb en Ajustes antes de buscar metadata."), 412);
    return k;
}

void saveSetting(const QString &key, const QString &value)
{
    if (value.isNull()) Store::exec(QStringLiteral("DELETE FROM app_settings WHERE clave = ?"), {key});
    else Store::exec(QStringLiteral("INSERT INTO app_settings (clave, valor) VALUES (?, ?) ON CONFLICT(clave) DO UPDATE SET valor = excluded.valor"), {key, value});
}

double numSetting(const QString &key, double fallback)
{
    bool ok = false;
    const double n = Store::scalar(QStringLiteral("SELECT valor FROM app_settings WHERE clave = ?"), {key}).toString().toDouble(&ok);
    return ok && n > 0 ? n : fallback;
}

// ---------------------------------------------------------------- bibliotecas

QJsonArray listLibraries()
{
    QJsonArray out;
    for (const QVariantMap &b : Store::rows(QStringLiteral("SELECT id, nombre, tipo, ruta_actual FROM libraries ORDER BY creado_en, nombre"))) {
        auto n = [&](const QString &sql) { return Store::scalar(sql, {b["id"]}).toLongLong(); };
        out.append(QJsonObject{{"id", b["id"].toString()}, {"nombre", b["nombre"].toString()}, {"tipo", b["tipo"].toString()}, {"ruta", b["ruta_actual"].toString()},
                               {"disponible", isDir(b["ruta_actual"].toString())},
                               {"items", n(QStringLiteral("SELECT COUNT(*) n FROM media_items WHERE library_id = ? AND estado_archivo = 'ok'"))},
                               {"series", n(QStringLiteral("SELECT COUNT(*) n FROM series WHERE library_id = ? AND estado_archivo = 'ok'"))},
                               {"huerfanos", n(QStringLiteral("SELECT COUNT(*) n FROM media_items WHERE library_id = ? AND estado_archivo != 'ok'")) + n(QStringLiteral("SELECT COUNT(*) n FROM series WHERE library_id = ? AND estado_archivo != 'ok'"))}});
    }
    return out;
}

QString validateName(const QJsonValue &v, const QString &exceptId = QStringLiteral(""))
{
    if (!v.isString() || v.toString().trimmed().isEmpty()) throw AdminError(QStringLiteral("El nombre de la biblioteca es obligatorio."));
    const QString n = v.toString().trimmed();
    if (Store::scalar(QStringLiteral("SELECT id FROM libraries WHERE lower(nombre) = lower(?) AND id != ?"), {n, exceptId}).isValid()) throw AdminError(QStringLiteral("Ya existe una biblioteca con ese nombre."), 409);
    return n;
}

QString validatePath(const QJsonValue &v, const QString &exceptId = QStringLiteral(""))
{
    if (!v.isString() || v.toString().trimmed().isEmpty()) throw AdminError(QStringLiteral("Elige la carpeta de la biblioteca."));
    const QString r = resolvePath(v.toString().trimmed());
    if (!isDir(r)) throw AdminError(QStringLiteral("La carpeta no existe o no es accesible."));
    for (const QVariantMap &o : Store::rows(QStringLiteral("SELECT id, nombre, ruta_actual FROM libraries WHERE id != ?"), {exceptId})) {
        const QString a = resolvePath(o["ruta_actual"].toString()).toLower(), b = r.toLower();
        if (a == b) throw AdminError(QStringLiteral("Esa carpeta ya pertenece a la biblioteca «%1».").arg(o["nombre"].toString()), 409);
        // Una dentro de otra duplicaría los archivos en dos bibliotecas.
        const QString sep = QString(QDir::separator());
        if (b.startsWith(a + sep) || a.startsWith(b + sep)) throw AdminError(QStringLiteral("La carpeta se superpone con la biblioteca «%1».").arg(o["nombre"].toString()), 409);
    }
    return r;
}

QString createLibrary(const QJsonObject &d)
{
    const QString type = d.value(QStringLiteral("tipo")).toString();
    if (type != QLatin1String("pelicula") && type != QLatin1String("serie")) throw AdminError(QStringLiteral("Tipo de biblioteca inválido."));
    const QString name = validateName(d.value(QStringLiteral("nombre")));
    const QString path = validatePath(d.value(QStringLiteral("ruta")));
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    Store::exec(QStringLiteral("INSERT INTO libraries (id, nombre, tipo, ruta_actual, creado_en) VALUES (?, ?, ?, ?, ?)"), {id, name, type, path, now()});
    return id;
}

/** Devuelve si cambió la carpeta (hay que reescanear para reconciliar por hash). El tipo no se edita. */
bool updateLibrary(const QString &id, const QJsonObject &d)
{
    const Library b = requireLibrary(id);
    const QString name = d.contains(QStringLiteral("nombre")) ? validateName(d.value(QStringLiteral("nombre")), id) : b.name;
    const QString path = d.contains(QStringLiteral("ruta")) ? validatePath(d.value(QStringLiteral("ruta")), id) : b.path;
    Store::exec(QStringLiteral("UPDATE libraries SET nombre = ?, ruta_actual = ? WHERE id = ?"), {name, path, id});
    return resolvePath(path).toLower() != resolvePath(b.path).toLower();
}

/** Quita la biblioteca y su catálogo (no toca archivos). Exige escribir el nombre exacto: borra progreso e historial. */
void deleteLibrary(const QString &id, const QString &confirm)
{
    const Library b = requireLibrary(id);
    if (confirm != b.name) throw AdminError(QStringLiteral("Escribe el nombre exacto de la biblioteca para confirmar."), 409);
    Store::begin();
    // Orden explícito: no se depende de que todas las FK del esquema heredado tengan ON DELETE CASCADE.
    Store::exec(QStringLiteral("DELETE FROM media_items WHERE library_id = ?"), {id});
    Store::exec(QStringLiteral("DELETE FROM series WHERE library_id = ?"), {id});
    Store::exec(QStringLiteral("DELETE FROM libraries WHERE id = ?"), {id});
    Store::commit();
}

struct Orphan { QString kind, id, rel; QVariant title, series; };

QList<Orphan> listOrphans(const QString &libId)
{
    requireLibrary(libId);
    QList<Orphan> out;
    for (const QVariantMap &i : Store::rows(QStringLiteral(
             "SELECT m.id, m.ruta_relativa, m.titulo, s.titulo AS serie_titulo, s.ruta_relativa AS serie_ruta FROM media_items m LEFT JOIN series s ON s.id = m.series_id"
             " WHERE m.library_id = ? AND m.estado_archivo != 'ok' ORDER BY m.ruta_relativa"), {libId}))
        out.append({QStringLiteral("item"), i["id"].toString(), i["ruta_relativa"].toString(), i["titulo"], i["serie_titulo"].isNull() ? i["serie_ruta"] : i["serie_titulo"]});
    for (const QVariantMap &s : Store::rows(QStringLiteral("SELECT id, ruta_relativa, titulo FROM series WHERE library_id = ? AND estado_archivo != 'ok' ORDER BY ruta_relativa"), {libId}))
        out.append({QStringLiteral("serie"), s["id"].toString(), s["ruta_relativa"].toString(), s["titulo"], nullVar()});
    return out;
}

QJsonObject archivoInfo(const QString &itemId)
{
    const QVariantMap f = Store::row(QStringLiteral("SELECT id, library_id, ruta_relativa FROM media_items WHERE id = ?"), {itemId});
    if (f.isEmpty()) throw AdminError(QStringLiteral("El ítem no existe."), 404);
    const Library lib = requireLibrary(f["library_id"].toString());
    const QFileInfo fi(QDir(lib.path).absoluteFilePath(f["ruta_relativa"].toString()));
    const bool exists = fi.isFile();
    return QJsonObject{{"bibliotecaId", lib.id}, {"bibliotecaNombre", lib.name}, {"carpetaBiblioteca", lib.path}, {"rutaRelativa", f["ruta_relativa"].toString()},
                       {"existe", exists}, {"bytes", exists ? QJsonValue(double(fi.size())) : QJsonValue(QJsonValue::Null)}};
}

/** Re-vincula un ítem sin archivo con otro que sí existe en la carpeta de su biblioteca; conserva metadata, progreso y colecciones. */
void relink(const QString &itemId, const QJsonValue &rel)
{
    if (!rel.isString() || rel.toString().trimmed().isEmpty()) throw AdminError(QStringLiteral("Indica la ruta del archivo."));
    const QVariantMap item = Store::row(QStringLiteral("SELECT id, library_id FROM media_items WHERE id = ?"), {itemId});
    if (item.isEmpty()) throw AdminError(QStringLiteral("El ítem no existe."), 404);
    const Library lib = requireLibrary(item["library_id"].toString());
    const QDir root(lib.path);
    const QString abs = QDir::cleanPath(root.absoluteFilePath(rel.toString()));
    const QString relPath = root.relativeFilePath(abs);
    // Que el archivo esté DENTRO de la biblioteca (evita `..` y rutas absolutas hacia otro lado del disco).
    if (relPath.isEmpty() || relPath.startsWith(QLatin1String("..")) || QFileInfo(relPath).isAbsolute()) throw AdminError(QStringLiteral("El archivo debe estar dentro de la carpeta de la biblioteca."));
    if (!QFileInfo(abs).isFile()) throw AdminError(QStringLiteral("El archivo no existe."), 404);
    QString nueva = relPath;
    nueva.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (Store::scalar(QStringLiteral("SELECT id FROM media_items WHERE library_id = ? AND ruta_relativa = ? AND estado_archivo = 'ok' AND id != ?"), {lib.id, nueva, itemId}).isValid())
        throw AdminError(QStringLiteral("Ese archivo ya está vinculado a otro ítem."), 409);
    Store::exec(QStringLiteral("UPDATE media_items SET ruta_relativa = ?, hash_respaldo = ?, estado_archivo = 'ok' WHERE id = ?"), {nueva, Scanner::backupHash(abs), itemId});
}

QString cleanFileName(const QString &t)
{
    QString s;
    for (const QChar c : t) s += (QString::fromLatin1("<>:\"/\\|?*").contains(c) || c.unicode() < 32) ? QLatin1Char(' ') : c;
    s.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    s.remove(QRegularExpression(QStringLiteral("[. ]+$")));
    return s.trimmed();
}

/** `Título (Año)/Título (Año).ext`: calcula el plan sin tocar el disco; con `apply` renombra carpeta y archivo (más subtítulos externos). */
QJsonObject homologate(const QString &itemId, const QJsonObject &d, bool apply)
{
    const QVariantMap item = Store::row(QStringLiteral("SELECT id, library_id, series_id, ruta_relativa, titulo, anio FROM media_items WHERE id = ?"), {itemId});
    if (item.isEmpty()) throw AdminError(QStringLiteral("El ítem no existe."), 404);
    if (!item["series_id"].isNull()) throw AdminError(QStringLiteral("Homologar el nombre solo aplica a películas."));
    const Library lib = requireLibrary(item["library_id"].toString());
    const QString rawTitle = d.value(QStringLiteral("titulo")).isString() ? d.value(QStringLiteral("titulo")).toString() : item["titulo"].toString();
    const QString title = cleanFileName(rawTitle);
    if (title.isEmpty()) throw AdminError(QStringLiteral("Para homologar el nombre la película necesita un título."));
    std::optional<int> year;
    if (!d.contains(QStringLiteral("anio"))) { if (!item["anio"].isNull()) year = item["anio"].toInt(); }
    else if (!(d.value(QStringLiteral("anio")).isNull() || (d.value(QStringLiteral("anio")).isString() && d.value(QStringLiteral("anio")).toString().isEmpty()))) {
        const double n = d.value(QStringLiteral("anio")).isString() ? d.value(QStringLiteral("anio")).toString().toDouble() : d.value(QStringLiteral("anio")).toDouble();
        if (n != int(n) || n < 1800 || n > 2200) throw AdminError(QStringLiteral("El año no es válido."));
        year = int(n);
    }
    const QString base = year ? QStringLiteral("%1 (%2)").arg(title).arg(*year) : title;
    const QStringList parts = item["ruta_relativa"].toString().split(QLatin1Char('/'));
    if (parts.size() != 2) throw AdminError(QStringLiteral("La película no está en una carpeta propia (Carpeta/Archivo): no se puede homologar automáticamente."));
    const QString oldFolder = parts.at(0), oldFile = parts.at(1);
    const QString ext = Naming::extName(oldFile);
    const QString oldBase = oldFile.left(oldFile.size() - ext.size());
    const QString nuevo = base + QLatin1Char('/') + base + ext;
    if (nuevo == item["ruta_relativa"].toString()) throw AdminError(QStringLiteral("El nombre ya está homologado."), 409);

    const QDir root(lib.path);
    const QString absOld = root.filePath(oldFolder), absNew = root.filePath(base);
    if (!isDir(absOld)) throw AdminError(QStringLiteral("La carpeta de la película no existe en el disco."), 404);
    const QString prefix = oldFolder + QLatin1Char('/');
    // `substr` y no `LIKE`: los nombres de carpeta pueden llevar % o _ y habría que escaparlos.
    if (Store::scalar(QStringLiteral("SELECT 1 FROM media_items WHERE library_id = ? AND id != ? AND substr(ruta_relativa, 1, ?) = ?"), {lib.id, itemId, int(prefix.size()), prefix}).isValid())
        throw AdminError(QStringLiteral("Hay otra película en esa misma carpeta: renombrarla afectaría a ambas."), 409);
    const bool sameFolder = QDir::cleanPath(absOld).compare(QDir::cleanPath(absNew), Qt::CaseInsensitive) == 0;   // mismo nombre salvo mayúsculas: es la misma carpeta
    if (!sameFolder && QFileInfo::exists(absNew)) throw AdminError(QStringLiteral("Ya existe una carpeta llamada «%1».").arg(base), 409);

    // Acompañantes: archivos con el mismo nombre base (Alien.es.srt → Nuevo.es.srt), distintos del principal.
    QStringList companions;
    for (const QFileInfo &e : QDir(absOld).entryInfoList(QDir::Files | QDir::Hidden | QDir::System, QDir::NoSort))
        if (e.fileName() != oldFile && e.fileName().toLower().startsWith(oldBase.toLower() + QLatin1Char('.'))) companions << e.fileName();
    QList<QPair<QString, QString>> renames{{oldFile, base + ext}};
    for (const QString &a : companions) renames.append({a, base + a.mid(oldBase.size())});
    for (const auto &r : renames)
        if (r.first.toLower() != r.second.toLower() && QFileInfo::exists(QDir(absOld).filePath(r.second)))
            throw AdminError(QStringLiteral("Ya existe un archivo llamado «%1» en la carpeta.").arg(r.second), 409);
    const QVector<QVariantMap> extras = Store::rows(QStringLiteral("SELECT id, ruta_relativa FROM extras WHERE media_item_id = ? AND substr(ruta_relativa, 1, ?) = ?"), {itemId, int(prefix.size()), prefix});
    const QJsonObject plan{{"actual", item["ruta_relativa"].toString()}, {"nuevo", nuevo}, {"acompanantes", int(companions.size())}, {"extras", int(extras.size())}};
    if (!apply) return plan;

    QList<QPair<QString, QString>> done;   // para deshacer en orden inverso si algo falla
    auto move = [&](const QString &from, const QString &to) {
        if (!QFile::rename(from, to)) throw std::runtime_error("no se pudo renombrar");
        done.append({from, to});
    };
    try {
        const QString newFile = base + ext;
        if (oldFile != newFile) move(QDir(absOld).filePath(oldFile), QDir(absOld).filePath(newFile));
        for (const QString &a : companions) move(QDir(absOld).filePath(a), QDir(absOld).filePath(base + a.mid(oldBase.size())));
        if (oldFolder != base) move(absOld, absNew);
        Store::begin();
        Store::exec(QStringLiteral("UPDATE media_items SET ruta_relativa = ? WHERE id = ?"), {nuevo, itemId});
        for (const QVariantMap &x : extras)
            Store::exec(QStringLiteral("UPDATE extras SET ruta_relativa = ? WHERE id = ?"), {base + QLatin1Char('/') + x["ruta_relativa"].toString().mid(oldFolder.size() + 1), x["id"]});
        Store::commit();
    } catch (const std::exception &e) {
        for (int i = int(done.size()) - 1; i >= 0; --i) QFile::rename(done.at(i).second, done.at(i).first);
        throw AdminError(QStringLiteral("No se pudo renombrar (%1). No se cambió nada.").arg(QString::fromUtf8(e.what())), 500);
    }
    return plan;
}

// ---------------------------------------------------------------- usuarios

constexpr int kMinPassword = 6;

void createUser(const QJsonObject &d, QString *idOut)
{
    const QString name = d.value(QStringLiteral("nombreUsuario")).isString() ? d.value(QStringLiteral("nombreUsuario")).toString().trimmed() : QString();
    if (name.size() < 3 || name.size() > 40) throw AdminError(QStringLiteral("El nombre de usuario debe tener entre 3 y 40 caracteres."));
    if (!d.value(QStringLiteral("password")).isString() || d.value(QStringLiteral("password")).toString().size() < kMinPassword)
        throw AdminError(QStringLiteral("La contraseña debe tener al menos %1 caracteres.").arg(kMinPassword));
    if (Store::scalar(QStringLiteral("SELECT 1 FROM usuarios WHERE lower(nombre_usuario) = lower(?)"), {name}).isValid()) throw AdminError(QStringLiteral("Ya existe un usuario con ese nombre."), 409);
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    Store::exec(QStringLiteral("INSERT INTO usuarios (id, nombre_usuario, password_hash, rol, creado_en) VALUES (?, ?, ?, 'cliente', ?)"), {id, name, Bcrypt::hash(d.value(QStringLiteral("password")).toString()), now()});
    *idOut = id;
}

// ---------------------------------------------------------------- datos

QJsonObject measure(const QString &folder)
{
    qint64 files = 0, bytes = 0;
    if (QFileInfo::exists(folder)) {
        QDirIterator it(folder, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (it.hasNext()) { it.next(); bytes += it.fileInfo().size(); files++; }
    }
    return QJsonObject{{"archivos", double(files)}, {"bytes", double(bytes)}};
}

/** Convierte excepciones de administración y de TMDb en la respuesta HTTP que corresponde. */
template <typename F>
HttpResponse guarded(F &&fn)
{
    try {
        return fn();
    } catch (const AdminError &e) {
        return HttpResponse::error(e.status, QString::fromUtf8(e.what()));
    } catch (const JobManager::Running &e) {
        return HttpResponse::json(QJsonObject{{"error", QString::fromUtf8(e.what())}, {"jobId", e.jobId}}, 409);
    } catch (const Metadata::TmdbError &e) {
        return HttpResponse::error(e.status == 401 ? 400 : 502, QString::fromUtf8(e.what()));
    } catch (const Scanner::FolderUnavailable &) {
        return HttpResponse::error(500, QStringLiteral("La carpeta de la biblioteca no está disponible."));
    }
}

QJsonObject scanResultJson(const Scanner::Result &r)
{
    QJsonArray byHash;
    for (const QString &id : r.reconciliadosPorHashIds) {
        const QVariantMap m = Store::row(QStringLiteral("SELECT id, titulo, ruta_relativa AS rutaRelativa FROM media_items WHERE id = ?"), {id});
        if (!m.isEmpty()) byHash.append(QJsonObject{{"id", m["id"].toString()}, {"titulo", nz(m["titulo"])}, {"rutaRelativa", m["rutaRelativa"].toString()}});
    }
    return QJsonObject{{"nuevos", r.nuevos}, {"reconciliados", r.reconciliados}, {"noEncontrados", r.noEncontrados}, {"extrasNuevos", r.extrasNuevos},
                       {"extrasReconciliados", r.extrasReconciliados}, {"seriesNuevas", r.seriesNuevas}, {"seriesReconciliadas", r.seriesReconciliadas},
                       {"seriesNoEncontradas", r.seriesNoEncontradas}, {"reconciliadosPorHash", byHash}};
}

/** Escaneo de una biblioteca dentro de un trabajo; el resultado incluye qué se reconcilió por hash para la pantalla de revisión. */
QJsonValue runScan(const Library &b, JobManager::Context &c)
{
    c.line(QStringLiteral("Biblioteca «%1» — %2").arg(b.name, b.path));
    Scanner::Options o;
    o.aborted = c.aborted;
    o.onProgress = [&](const Scanner::Progress &p) {
        if (p.phase == QLatin1String("listando")) c.line(QStringLiteral("Listando archivos… %1/%2").arg(p.done).arg(p.total));
        else c.progress(p.done, p.total);
    };
    try {
        const Scanner::Result r = Scanner::scanLibrary({b.id, b.type, b.path}, o);
        c.line(QStringLiteral("Listo: %1 nuevo(s), %2 reconciliado(s), %3 sin archivo").arg(r.nuevos).arg(r.reconciliados).arg(r.noEncontrados)
               + (b.type == QLatin1String("serie") ? QStringLiteral(", %1 serie(s) nueva(s)").arg(r.seriesNuevas) : QStringLiteral(", %1 extra(s) nuevo(s)").arg(r.extrasNuevos)));
        return scanResultJson(r);
    } catch (const Scanner::FolderUnavailable &e) {
        c.line(QStringLiteral("No se pudo escanear: ") + QString::fromUtf8(e.what()));
        throw std::runtime_error("La carpeta de la biblioteca no está disponible (¿disco desconectado?). No se modificó nada.");
    }
}

} // namespace

void registerAdminRoutes(HttpServer &h, JobManager &jobs)
{
    const QString A = QStringLiteral("/api/v1/admin");
    auto scan = [&jobs](const Library &b) {
        return jobs.start(QStringLiteral("escaneo"), b.id, QStringLiteral("Escaneando «%1»").arg(b.name), [b](JobManager::Context &c) { return runScan(b, c); });
    };

    // ------------------------------------------------------------- trabajos
    h.route("POST", A + "/bibliotecas/:id/escanear", [&jobs, scan](HttpRequest &r) { return guarded([&] { return HttpResponse::json(scan(requireLibrary(r.params["id"]))); }); });
    h.route("POST", A + "/escanear-todo", [&jobs](HttpRequest &) {
        return guarded([&] {
            QList<Library> all;
            for (const QVariantMap &b : Store::rows(QStringLiteral("SELECT id, nombre, tipo, ruta_actual FROM libraries ORDER BY creado_en")))
                all.append({b["id"].toString(), b["nombre"].toString(), b["tipo"].toString(), b["ruta_actual"].toString()});
            if (all.isEmpty()) throw AdminError(QStringLiteral("Aún no hay bibliotecas."));
            return HttpResponse::json(jobs.start(QStringLiteral("escaneo"), nullVar(), QStringLiteral("Escaneando todas las bibliotecas"), [all](JobManager::Context &c) {
                QJsonArray results;
                for (const Library &b : all) {
                    if (c.aborted()) break;
                    try {
                        QJsonObject o = runScan(b, c).toObject();
                        o.insert(QStringLiteral("biblioteca"), b.name);
                        results.append(o);
                    } catch (const std::exception &e) {
                        c.line(QStringLiteral("«%1»: %2").arg(b.name, QString::fromUtf8(e.what())));
                    }
                }
                return QJsonValue(QJsonObject{{"bibliotecas", results}});
            }));
        });
    });
    h.route("POST", A + "/bibliotecas/:id/metadata", [&jobs](HttpRequest &r) {
        return guarded([&] {
            const Library b = requireLibrary(r.params["id"]);
            const QString mode = bodyObj(r).value(QStringLiteral("modo")).toString();
            if (mode != QLatin1String("faltantes") && mode != QLatin1String("todo")) throw AdminError(QStringLiteral("modo debe ser 'faltantes' o 'todo'."));
            const QString key = tmdbKey();
            return HttpResponse::json(jobs.start(QStringLiteral("metadata"), b.id, QStringLiteral("Metadata de «%1»").arg(b.name), [b, key, mode](JobManager::Context &c) {
                Metadata::ImportOptions o;
                o.onlyMissing = mode == QLatin1String("faltantes");
                o.aborted = c.aborted;
                o.onEvent = [&c](const Metadata::ImportEvent &e) { if (e.isLine) c.line(e.text); else c.progress(e.processed, e.total); };
                return QJsonValue(Metadata::importLibrary(b.id, b.type, key, o));
            }));
        });
    });
    h.route("GET", A + "/jobs", [&jobs](HttpRequest &) { return HttpResponse::json(jobs.list()); });
    h.route("GET", A + "/jobs/:id", [&jobs](HttpRequest &r) {
        return guarded([&] {
            if (!jobs.exists(r.params["id"])) throw AdminError(QStringLiteral("El trabajo no existe."), 404);
            return HttpResponse::json(jobs.summary(r.params["id"], true));
        });
    });
    h.route("POST", A + "/jobs/:id/cancelar", [&jobs](HttpRequest &r) {
        return guarded([&] {
            if (!jobs.exists(r.params["id"])) throw AdminError(QStringLiteral("El trabajo no existe."), 404);
            return HttpResponse::json(QJsonObject{{"cancelado", jobs.cancel(r.params["id"])}});
        });
    });
    // SSE: `job` (estado/progreso) y `linea` (registro). Al conectar manda el estado actual de los trabajos.
    h.route("GET", A + "/jobs-eventos", [&jobs](HttpRequest &) {
        HttpResponse res;
        res.sse = [&jobs](SseChannel *ch) {
            for (const QJsonValue &j : jobs.list()) ch->send(QStringLiteral("job"), j);
            QObject::connect(&jobs, &JobManager::changed, ch, [ch](const QJsonObject &s) { ch->send(QStringLiteral("job"), s); });
            QObject::connect(&jobs, &JobManager::line, ch, [ch](const QJsonObject &l) { ch->send(QStringLiteral("linea"), l); });
        };
        return res;
    });

    // ------------------------------------------------------------- bibliotecas
    h.route("GET", A + "/bibliotecas", [](HttpRequest &) { return HttpResponse::json(listLibraries()); });
    h.route("POST", A + "/bibliotecas", [&jobs, scan](HttpRequest &r) {
        return guarded([&] {
            const QJsonObject b = bodyObj(r);
            const QString type = b.value(QStringLiteral("tipo")).toString();
            if (type != QLatin1String("pelicula") && type != QLatin1String("serie")) throw AdminError(QStringLiteral("Tipo de biblioteca inválido."));
            const QString id = createLibrary(b);
            const QJsonValue job = b.value(QStringLiteral("escanear")).isBool() && !b.value(QStringLiteral("escanear")).toBool() ? QJsonValue(QJsonValue::Null) : QJsonValue(scan(requireLibrary(id)));
            return HttpResponse::json(QJsonObject{{"id", id}, {"job", job}}, 201);
        });
    });
    h.route("PUT", A + "/bibliotecas/:id", [&jobs, scan](HttpRequest &r) {
        return guarded([&] {
            const QString id = r.params["id"];
            const bool pathChanged = updateLibrary(id, bodyObj(r));
            // Cambiar de carpeta dispara un escaneo: lo que se movió se reconcilia por hash sin perder progreso ni metadata.
            return HttpResponse::json(QJsonObject{{"id", id}, {"job", pathChanged ? QJsonValue(scan(requireLibrary(id))) : QJsonValue(QJsonValue::Null)}});
        });
    });
    h.route("DELETE", A + "/bibliotecas/:id", [&jobs](HttpRequest &r) {
        return guarded([&] {
            const QString id = r.params["id"];
            if (jobs.busyFor(id)) throw AdminError(QStringLiteral("Hay un trabajo en curso para esta biblioteca. Espera a que termine o cancélalo."), 409);
            deleteLibrary(id, r.q(QStringLiteral("confirm")));
            return HttpResponse::json(QJsonObject{{"ok", true}});
        });
    });

    // ------------------------------------------------------------- huérfanos y re-vinculación
    h.route("GET", A + "/bibliotecas/:id/huerfanos", [](HttpRequest &r) {
        return guarded([&] {
            QJsonArray out;
            for (const Orphan &o : listOrphans(r.params["id"]))
                out.append(QJsonObject{{"tipo", o.kind}, {"id", o.id}, {"rutaRelativa", o.rel}, {"titulo", nz(o.title)}, {"serie", nz(o.series)}});
            return HttpResponse::json(out);
        });
    });
    h.route("DELETE", A + "/bibliotecas/:id/huerfanos", [](HttpRequest &r) {
        return guarded([&] {
            const QList<Orphan> list = listOrphans(r.params["id"]);
            if (r.q(QStringLiteral("confirm")).toInt() != list.size() || list.isEmpty()) throw AdminError(QStringLiteral("La cantidad a eliminar cambió; revisa la lista y confirma de nuevo."), 409);
            Store::begin();
            for (const Orphan &o : list) Store::exec(QStringLiteral("DELETE FROM %1 WHERE id = ?").arg(o.kind == QLatin1String("item") ? QStringLiteral("media_items") : QStringLiteral("series")), {o.id});
            Store::commit();
            return HttpResponse::json(QJsonObject{{"eliminados", int(list.size())}});
        });
    });
    h.route("GET", A + "/items/:id/archivo", [](HttpRequest &r) { return guarded([&] { return HttpResponse::json(archivoInfo(r.params["id"])); }); });
    h.route("POST", A + "/items/:id/revincular", [](HttpRequest &r) {
        return guarded([&] { relink(r.params["id"], bodyObj(r).value(QStringLiteral("rutaRelativa"))); return HttpResponse::json(QJsonObject{{"ok", true}}); });
    });
    // Sin `aplicar` solo previsualiza (nombre actual → nuevo); con `aplicar: true` renombra en el disco.
    h.route("POST", A + "/items/:id/homologar", [&jobs](HttpRequest &r) {
        return guarded([&] {
            const QJsonObject b = bodyObj(r);
            const bool apply = b.value(QStringLiteral("aplicar")).toBool();
            if (apply) {
                const QVariantMap bib = Store::row(QStringLiteral("SELECT library_id FROM media_items WHERE id = ?"), {r.params["id"]});
                if (!bib.isEmpty() && jobs.busyFor(bib["library_id"].toString())) throw AdminError(QStringLiteral("Hay un escaneo en curso: espera a que termine para renombrar."), 409);
            }
            return HttpResponse::json(homologate(r.params["id"], b, apply));
        });
    });

    // ------------------------------------------------------------- metadata
    auto tableOf = [](const QString &c) { return c == QLatin1String("series") ? QStringLiteral("series") : QStringLiteral("media_items"); };
    auto requireExists = [](const QString &table, const QString &id) {
        if (!Store::scalar(QStringLiteral("SELECT 1 FROM %1 WHERE id = ?").arg(table), {id}).isValid()) throw AdminError(QStringLiteral("No existe."), 404);
    };
    h.route("PUT", A + "/:coleccion/:id/metadata", [tableOf, requireExists](HttpRequest &r) {
        return guarded([&] {
            const QString col = r.params["coleccion"];
            if (col != QLatin1String("items") && col != QLatin1String("series")) throw AdminError(QStringLiteral("Ruta no encontrada."), 404);
            requireExists(tableOf(col), r.params["id"]);
            Metadata::editMetadata(tableOf(col), r.params["id"], bodyObj(r));
            return HttpResponse::json(QJsonObject{{"ok", true}});
        });
    });
    h.routeAsync("GET", A + "/tmdb/buscar", [&h](HttpRequest &r, std::function<void(HttpResponse)> done) {
        const QString type = r.q(QStringLiteral("tipo")), q = r.q(QStringLiteral("q")), year = r.q(QStringLiteral("anio"));
        if (type != QLatin1String("pelicula") && type != QLatin1String("serie")) { done(HttpResponse::error(400, QStringLiteral("tipo inválido."))); return; }
        QString key;
        try { key = tmdbKey(); } catch (const AdminError &e) { done(HttpResponse::error(e.status, QString::fromUtf8(e.what()))); return; }
        bool ok = false;
        const int y = year.toInt(&ok);
        h.runBlocking([type, q, key, y, ok] { return guarded([&] { return HttpResponse::json(Metadata::search(key, type, q, ok ? y : 0)); }); }, done);
    });
    h.routeAsync("POST", A + "/:coleccion/:id/tmdb", [&h, tableOf, requireExists](HttpRequest &r, std::function<void(HttpResponse)> done) {
        const QString col = r.params["coleccion"], id = r.params["id"];
        if (col != QLatin1String("items") && col != QLatin1String("series")) { done(HttpResponse::error(404, QStringLiteral("Ruta no encontrada."))); return; }
        const QJsonValue tv = bodyObj(r).value(QStringLiteral("tmdbId"));
        const double t = tv.isString() ? tv.toString().toDouble() : tv.toDouble(0);
        if (t != int(t) || t <= 0) { done(HttpResponse::error(400, QStringLiteral("tmdbId inválido."))); return; }
        const HttpResponse pre = guarded([&] { requireExists(tableOf(col), id); tmdbKey(); return HttpResponse(); });
        if (pre.status != 200) { done(pre); return; }
        h.runBlocking([col, id, t] {
            return guarded([&] {
                Metadata::assignManual(col == QLatin1String("series") ? QStringLiteral("serie") : QStringLiteral("item"), id, int(t), tmdbKey());
                return HttpResponse::json(QJsonObject{{"ok", true}});
            });
        }, done);
    });

    // ------------------------------------------------------------- ajustes
    h.route("GET", A + "/ajustes/subtitulos", [](HttpRequest &) {
        return HttpResponse::json(QJsonObject{{"tamano", numSetting(QStringLiteral("subtitle_font_size"), 32)}, {"posicion", numSetting(QStringLiteral("subtitle_vertical_offset"), 24)}});
    });
    h.route("PUT", A + "/ajustes/subtitulos", [](HttpRequest &r) {
        return guarded([&] {
            const QJsonObject b = bodyObj(r);
            auto ok = [](const QJsonValue &v) { return v.isDouble() && v.toDouble() >= 0 && v.toDouble() <= 500; };
            if (b.contains(QStringLiteral("tamano"))) { if (!ok(b.value(QStringLiteral("tamano")))) throw AdminError(QStringLiteral("tamano inválido.")); saveSetting(QStringLiteral("subtitle_font_size"), QString::number(b.value(QStringLiteral("tamano")).toDouble())); }
            if (b.contains(QStringLiteral("posicion"))) { if (!ok(b.value(QStringLiteral("posicion")))) throw AdminError(QStringLiteral("posicion inválida.")); saveSetting(QStringLiteral("subtitle_vertical_offset"), QString::number(b.value(QStringLiteral("posicion")).toDouble())); }
            return HttpResponse::json(QJsonObject{{"tamano", numSetting(QStringLiteral("subtitle_font_size"), 32)}, {"posicion", numSetting(QStringLiteral("subtitle_vertical_offset"), 24)}});
        });
    });
    h.route("GET", A + "/ajustes/tmdb", [](HttpRequest &) {
        const QString k = Store::scalar(QStringLiteral("SELECT valor FROM app_settings WHERE clave = 'tmdb_api_key'")).toString().trimmed();
        return HttpResponse::json(QJsonObject{{"configurada", !k.isEmpty()}, {"mascara", maskJson(k)}});
    });
    h.route("PUT", A + "/ajustes/tmdb", [](HttpRequest &r) {
        return guarded([&] {
            const QJsonValue k = bodyObj(r).value(QStringLiteral("apiKey"));
            if (!k.isNull() && !k.isString()) throw AdminError(QStringLiteral("apiKey inválida."));
            const QString clean = k.isString() ? k.toString().trimmed() : QString();
            saveSetting(QStringLiteral("tmdb_api_key"), clean.isEmpty() ? QString() : clean);
            return HttpResponse::json(QJsonObject{{"configurada", !clean.isEmpty()}, {"mascara", maskJson(clean)}});
        });
    });
    // Prueba la clave enviada o, si no se manda, la guardada.
    h.routeAsync("POST", A + "/ajustes/tmdb/probar", [&h](HttpRequest &r, std::function<void(HttpResponse)> done) {
        const QJsonValue k = bodyObj(r).value(QStringLiteral("apiKey"));
        QString key = k.isString() ? k.toString().trimmed() : QString();
        if (key.isEmpty()) {
            try { key = tmdbKey(); } catch (const AdminError &e) { done(HttpResponse::error(e.status, QString::fromUtf8(e.what()))); return; }
        }
        h.runBlocking([key] { return guarded([&] { Metadata::testKey(key); return HttpResponse::json(QJsonObject{{"ok", true}}); }); }, done);
    });

    // ------------------------------------------------------------- datos (caché y temporales)
    h.route("GET", A + "/datos", [](HttpRequest &) {
        const QString folder = Store::folder();
        return HttpResponse::json(QJsonObject{{"carpeta", QDir::toNativeSeparators(folder)}, {"imagenes", measure(folder + QStringLiteral("/images/cl"))}, {"temporales", measure(folder + QStringLiteral("/transcode"))},
                                              {"baseBytes", double(QFileInfo(folder + QStringLiteral("/cinemalove.sqlite")).size())}});
    });
    h.route("POST", A + "/datos/limpiar/:que", [](HttpRequest &r) {
        return guarded([&] {
            const QString what = r.params["que"], folder = Store::folder();
            if (what == QLatin1String("imagenes")) {
                const QJsonObject before = measure(folder + QStringLiteral("/images/cl"));
                QDir(folder + QStringLiteral("/images/cl")).removeRecursively();
                if (!Store::readOnly()) Store::exec(QStringLiteral("DELETE FROM cl_image_cache"));
                return HttpResponse::json(before);
            }
            if (what == QLatin1String("temporales")) {
                const QJsonObject before = measure(folder + QStringLiteral("/transcode"));
                QDir d(folder + QStringLiteral("/transcode"));
                for (const QString &f : d.entryList(QDir::NoDotAndDotDot | QDir::AllEntries)) { QFileInfo(d.filePath(f)).isDir() ? QDir(d.filePath(f)).removeRecursively() : d.remove(f); }
                return HttpResponse::json(before);
            }
            throw AdminError(QStringLiteral("Solo se puede limpiar «imagenes» o «temporales»."));
        });
    });

    // ------------------------------------------------------------- usuarios y token de acceso
    h.route("GET", A + "/usuarios", [](HttpRequest &) {
        QJsonArray out;
        for (const QVariantMap &f : Store::rows(QStringLiteral("SELECT u.id, u.nombre_usuario, u.rol, u.creado_en, (SELECT COUNT(*) FROM sesiones s WHERE s.usuario_id = u.id AND s.expira_en > ?) AS sesiones FROM usuarios u ORDER BY lower(u.nombre_usuario)"), {now()}))
            out.append(QJsonObject{{"id", f["id"].toString()}, {"nombreUsuario", f["nombre_usuario"].toString()}, {"rol", f["rol"].toString()}, {"creadoEn", isoDate(f["creado_en"].toLongLong())}, {"sesiones", f["sesiones"].toInt()}});
        return HttpResponse::json(out);
    });
    h.route("POST", A + "/usuarios", [](HttpRequest &r) { return guarded([&] { QString id; createUser(bodyObj(r), &id); return HttpResponse::json(QJsonObject{{"id", id}}, 201); }); });
    h.route("PUT", A + "/usuarios/:id/password", [](HttpRequest &r) {
        return guarded([&] {
            const QJsonValue p = bodyObj(r).value(QStringLiteral("password"));
            if (!p.isString() || p.toString().size() < kMinPassword) throw AdminError(QStringLiteral("La contraseña debe tener al menos %1 caracteres.").arg(kMinPassword));
            if (!Store::scalar(QStringLiteral("SELECT 1 FROM usuarios WHERE id = ?"), {r.params["id"]}).isValid()) throw AdminError(QStringLiteral("El usuario no existe."), 404);
            Store::begin();
            Store::exec(QStringLiteral("UPDATE usuarios SET password_hash = ? WHERE id = ?"), {Bcrypt::hash(p.toString()), r.params["id"]});
            Store::exec(QStringLiteral("DELETE FROM sesiones WHERE usuario_id = ?"), {r.params["id"]});   // cambiar la contraseña cierra todas sus sesiones
            Store::commit();
            return HttpResponse::json(QJsonObject{{"ok", true}});
        });
    });
    h.route("DELETE", A + "/usuarios/:id", [](HttpRequest &r) {
        return guarded([&] {
            const QVariantMap u = Store::row(QStringLiteral("SELECT nombre_usuario FROM usuarios WHERE id = ?"), {r.params["id"]});
            if (u.isEmpty()) throw AdminError(QStringLiteral("El usuario no existe."), 404);
            if (r.q(QStringLiteral("confirm")) != u["nombre_usuario"].toString()) throw AdminError(QStringLiteral("Escribe el nombre exacto del usuario para confirmar."), 409);
            Store::begin();
            Store::exec(QStringLiteral("DELETE FROM sesiones WHERE usuario_id = ?"), {r.params["id"]});
            Store::exec(QStringLiteral("DELETE FROM usuarios WHERE id = ?"), {r.params["id"]});
            Store::commit();
            return HttpResponse::json(QJsonObject{{"ok", true}});
        });
    });
    h.route("GET", A + "/api-key", [](HttpRequest &) {
        const QVariant v = Store::scalar(QStringLiteral("SELECT valor FROM api_keys WHERE activa = 1 LIMIT 1"));
        return HttpResponse::json(QJsonObject{{"valor", v.isValid() ? QJsonValue(v.toString()) : QJsonValue(QJsonValue::Null)}});
    });
    h.route("POST", A + "/api-key/regenerar", [](HttpRequest &) {
        QByteArray b(32, 0);
        for (int i = 0; i < 32; ++i) b[i] = char(QRandomGenerator::system()->bounded(256));
        const QString value = QString::fromLatin1(b.toHex());
        Store::begin();
        Store::exec(QStringLiteral("DELETE FROM api_keys"));
        Store::exec(QStringLiteral("INSERT INTO api_keys (id, valor, activa, creado_en) VALUES (?, ?, 1, ?)"), {QUuid::createUuid().toString(QUuid::WithoutBraces), value, now()});
        Store::commit();
        return HttpResponse::json(QJsonObject{{"valor", value}});
    });
}
