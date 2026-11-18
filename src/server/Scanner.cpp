#include "Scanner.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>

#include "Naming.h"
#include "Store.h"

namespace Scanner {

namespace {

const QSet<QString> kVideoExt = {QStringLiteral(".mp4"), QStringLiteral(".mkv"), QStringLiteral(".avi"), QStringLiteral(".mov"), QStringLiteral(".m4v"), QStringLiteral(".wmv"),
                                 QStringLiteral(".flv"), QStringLiteral(".webm"), QStringLiteral(".ts"), QStringLiteral(".mpg"), QStringLiteral(".mpeg")};

struct FileEntry {
    QString abs;       // ruta absoluta
    QString rel;       // relativa a la raíz de la biblioteca, con «/»
    qint64 size = 0;
};

QString relativeTo(const QString &root, const QString &path)
{
    return QDir(root).relativeFilePath(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
}

QVariant nullVar() { return QVariant(QMetaType::fromType<QString>()); }
QVariant orNull(const std::optional<int> &v) { return v ? QVariant(*v) : nullVar(); }

/** Videos de un árbol, sin seguir enlaces simbólicos. Una carpeta ilegible se ignora (no se aborta el escaneo entero). */
void listVideos(const QString &dir, const QString &root, QList<FileEntry> *out)
{
    const QFileInfoList entries = QDir(dir).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::NoSort);
    for (const QFileInfo &e : entries) {
        if (e.isSymLink()) continue;
        if (e.isDir()) listVideos(e.absoluteFilePath(), root, out);
        else if (e.isFile() && isVideo(e.fileName())) out->append({e.absoluteFilePath(), relativeTo(root, e.absoluteFilePath()), e.size()});
    }
}

const FileEntry &heaviest(const QList<FileEntry> &files)
{
    const FileEntry *m = &files.first();
    for (const FileEntry &f : files) if (f.size > m->size) m = &f;
    return *m;
}

QString dirName(const QString &path) { return QFileInfo(path).absolutePath(); }

int seasonOfFile(const QString &file, const QString &seriesRoot)
{
    QString folder = dirName(file);
    while (!samePath(folder, seriesRoot)) {
        if (const auto n = Naming::seasonOfFolder(QFileInfo(folder).fileName())) return *n;
        const QString parent = dirName(folder);
        if (samePath(parent, folder)) break;   // raíz del filesystem
        folder = parent;
    }
    const auto ex = Naming::explicitEpisode(QFileInfo(file).fileName());
    return ex && ex->season ? *ex->season : 1;
}

/** Numera los episodios de un grupo: si TODOS traen número explícito y no hay duplicados se usan; si no, 1..N por orden natural. */
QList<QPair<FileEntry, int>> numberEpisodes(const QList<FileEntry> &files)
{
    QList<QPair<FileEntry, int>> explicitList;
    bool complete = true;
    QSet<int> seen;
    for (const FileEntry &f : files) {
        const auto m = Naming::explicitEpisode(QFileInfo(f.abs).fileName());
        if (!m) { complete = false; break; }
        explicitList.append({f, m->episode});
        seen.insert(m->episode);
    }
    if (complete && seen.size() == explicitList.size()) return explicitList;
    QList<FileEntry> sorted = files;
    std::stable_sort(sorted.begin(), sorted.end(), [](const FileEntry &a, const FileEntry &b) { return Naming::compareNatural(QFileInfo(a.abs).fileName(), QFileInfo(b.abs).fileName()) < 0; });
    QList<QPair<FileEntry, int>> out;
    for (int i = 0; i < sorted.size(); ++i) out.append({sorted.at(i), i + 1});
    return out;
}

struct MoviePlan { QString folder; FileEntry official; QList<FileEntry> extras; };
struct EpisodePlan { FileEntry file; int season; int episode; };
struct SeriesPlan { QString folder; QList<EpisodePlan> episodes; };

}

bool isVideo(const QString &path) { return kVideoExt.contains(Naming::extName(QFileInfo(path).fileName()).toLower()); }

bool samePath(const QString &a, const QString &b)
{
    const QString x = QDir::cleanPath(QFileInfo(a).absoluteFilePath()), y = QDir::cleanPath(QFileInfo(b).absoluteFilePath());
#ifdef Q_OS_WIN
    return x.compare(y, Qt::CaseInsensitive) == 0;
#else
    return x == y;
#endif
}

QString backupHash(const QString &path)
{
    constexpr qint64 kBlock = 64 * 1024;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const qint64 size = f.size();
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(f.read(qMin(kBlock, size)));
    if (size > kBlock) {
        f.seek(size - kBlock);
        h.addData(f.read(kBlock));
    }
    h.addData(QByteArray::number(size));
    return QString::fromLatin1(h.result().toHex());
}

Result scanLibrary(const Library &lib, const Options &opts)
{
    auto aborted = [&] { return opts.aborted && opts.aborted(); };
    auto progress = [&](const Progress &p) { if (opts.onProgress) opts.onProgress(p); };
    // Con la carpeta inalcanzable (disco de red apagado) marcar todo como «no encontrado» ocultaría la colección entera del Catálogo por un
    // problema pasajero: se avisa y no se toca nada.
    if (!QFileInfo::exists(lib.path) || !QFileInfo(lib.path).isDir()) throw FolderUnavailable(lib.path);

    // ---- plan (solo lectura del disco)
    progress({QStringLiteral("listando"), 0, 0, {}});
    QList<MoviePlan> movies;
    QList<SeriesPlan> seriesPlans;
    {
        const QFileInfoList folders = QDir(lib.path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::NoSort);
        int doneFolders = 0;
        for (const QFileInfo &c : folders) {
            if (aborted()) throw Cancelled();
            const QString folder = c.absoluteFilePath();
            QList<FileEntry> videos;
            listVideos(folder, lib.path, &videos);
            progress({QStringLiteral("listando"), ++doneFolders, int(folders.size()), {}});
            if (videos.isEmpty()) continue;
            if (lib.type == QLatin1String("serie")) {
                QList<int> order;
                QHash<int, QList<FileEntry>> bySeason;
                for (const FileEntry &v : videos) {
                    const int t = seasonOfFile(v.abs, folder);
                    if (!bySeason.contains(t)) order << t;
                    bySeason[t].append(v);
                }
                SeriesPlan sp{relativeTo(lib.path, folder), {}};
                for (const int t : order)
                    for (const auto &pr : numberEpisodes(bySeason.value(t))) sp.episodes.append({pr.first, t, pr.second});
                seriesPlans.append(sp);
            } else {
                QList<FileEntry> direct;
                for (const FileEntry &v : videos) if (samePath(dirName(v.abs), folder)) direct.append(v);
                // Sin video directo en la carpeta (estructura atípica) se recurre a todo el árbol para no perder el contenido.
                const FileEntry official = heaviest(direct.isEmpty() ? videos : direct);
                MoviePlan mp{relativeTo(lib.path, folder), official, {}};
                for (const FileEntry &v : videos) if (v.abs != official.abs) mp.extras.append(v);
                movies.append(mp);
            }
        }
    }
    int total = 0;
    for (const auto &m : movies) total += 1 + int(m.extras.size());
    for (const auto &s : seriesPlans) total += int(s.episodes.size());
    int done = 0;
    auto step = [&](const QString &rel) {
        progress({QStringLiteral("procesando"), ++done, total, rel});
        if (aborted()) throw Cancelled();
    };

    Result r;
    const QString libId = lib.id;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QVector<QVariantMap> existing = Store::rows(QStringLiteral("SELECT * FROM media_items WHERE library_id = ?"), {libId});
    QVector<QVariantMap> extrasExisting = Store::rows(QStringLiteral("SELECT e.* FROM extras e JOIN media_items m ON m.id = e.media_item_id WHERE m.library_id = ?"), {libId});
    QVector<QVariantMap> seriesExisting = Store::rows(QStringLiteral("SELECT * FROM series WHERE library_id = ?"), {libId});
    QSet<QString> seen, extrasSeen, seriesSeen;

    // El hash solo se necesita para lo que no casa por ruta (nuevo o movido): se precalcula en paralelo.
    QSet<QString> knownPaths, knownExtra;
    for (const QVariantMap &m : existing) knownPaths.insert(m["ruta_relativa"].toString());
    for (const QVariantMap &e : extrasExisting) knownExtra.insert(e["media_item_id"].toString() + QLatin1Char('|') + e["ruta_relativa"].toString());
    QList<FileEntry> toHash;
    auto needsHash = [&](const FileEntry &f) {
        if (knownPaths.contains(f.rel)) return false;
        for (const QString &k : std::as_const(knownExtra)) if (k.endsWith(QLatin1Char('|') + f.rel)) return false;
        return true;
    };
    for (const auto &m : movies) { if (needsHash(m.official)) toHash.append(m.official); for (const auto &x : m.extras) if (needsHash(x)) toHash.append(x); }
    for (const auto &s : seriesPlans) for (const auto &e : s.episodes) if (needsHash(e.file)) toHash.append(e.file);
    QHash<QString, QString> hashes;
    {
        QList<QString> paths;
        for (const FileEntry &f : toHash) paths << f.abs;
        const QList<QString> results = QtConcurrent::blockingMapped(paths, [&](const QString &p) { return aborted() ? QString() : backupHash(p); });
        for (int i = 0; i < paths.size(); ++i) hashes.insert(paths.at(i), results.at(i));
    }
    auto hashOf = [&](const FileEntry &f) {
        auto it = hashes.constFind(f.abs);
        if (it == hashes.constEnd()) it = hashes.insert(f.abs, backupHash(f.abs));
        return it.value();
    };

    int opsSinceCommit = 0;
    auto tick = [&] { if (++opsSinceCommit >= 100) { Store::commit(); Store::begin(); opsSinceCommit = 0; } };
    Store::begin();
    struct TxGuard { ~TxGuard() { Store::commit(); } } guard;   // lo escrito hasta una cancelación o un error se conserva, como en la versión Node

    struct ItemExtra { QVariant seriesId, season, episode; };
    auto reconcileItem = [&](const FileEntry &a, const ItemExtra &x, bool *isNew, QString *idByHash) {
        *isNew = false;
        idByHash->clear();
        for (const QVariantMap &m : existing) {
            if (m["ruta_relativa"].toString() != a.rel) continue;
            seen.insert(m["id"].toString());
            const bool stateChanged = m["estado_archivo"].toString() != QLatin1String("ok");
            const bool serieChanged = m["series_id"] != x.seriesId || m["numero_temporada"] != x.season || m["numero_episodio"] != x.episode;
            // Comparación como en JS (null === null): QVariant nulos de distinto tipo se consideran iguales
            const bool sidDiff = !(m["series_id"].isNull() && x.seriesId.isNull()) && m["series_id"].toString() != x.seriesId.toString();
            const bool seaDiff = !(m["numero_temporada"].isNull() && x.season.isNull()) && (m["numero_temporada"].isNull() != x.season.isNull() || m["numero_temporada"].toInt() != x.season.toInt());
            const bool epDiff = !(m["numero_episodio"].isNull() && x.episode.isNull()) && (m["numero_episodio"].isNull() != x.episode.isNull() || m["numero_episodio"].toInt() != x.episode.toInt());
            Q_UNUSED(serieChanged)
            if (stateChanged || sidDiff || seaDiff || epDiff)
                Store::exec(QStringLiteral("UPDATE media_items SET estado_archivo = 'ok', series_id = ?, numero_temporada = ?, numero_episodio = ?, actualizado_en = ? WHERE id = ?"),
                            {x.seriesId, x.season, x.episode, now, m["id"]});
            tick();
            return;
        }
        const QString h = hashOf(a);
        for (const QVariantMap &c : existing) {
            if (seen.contains(c["id"].toString()) || c["hash_respaldo"].isNull() || c["hash_respaldo"].toString() != h) continue;
            seen.insert(c["id"].toString());
            Store::exec(QStringLiteral("UPDATE media_items SET ruta_relativa = ?, estado_archivo = 'ok', hash_respaldo = ?, series_id = ?, numero_temporada = ?, numero_episodio = ?, actualizado_en = ? WHERE id = ?"),
                        {a.rel, h, x.seriesId, x.season, x.episode, now, c["id"]});
            *idByHash = c["id"].toString();
            tick();
            return;
        }
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        Store::exec(QStringLiteral("INSERT INTO media_items (id, library_id, ruta_relativa, estado_archivo, hash_respaldo, series_id, numero_temporada, numero_episodio, creado_en, actualizado_en) VALUES (?, ?, ?, 'ok', ?, ?, ?, ?, ?, ?)"),
                    {id, libId, a.rel, h, x.seriesId, x.season, x.episode, now, now});
        // La fila nueva entra a `existing` para que un archivo repetido en el mismo escaneo no se inserte dos veces
        seen.insert(id);
        *isNew = true;
        tick();
    };

    auto reconcileExtra = [&](const QString &mediaItemId, const FileEntry &a) {
        for (const QVariantMap &e : extrasExisting) {
            if (e["media_item_id"].toString() != mediaItemId || e["ruta_relativa"].toString() != a.rel) continue;
            extrasSeen.insert(e["id"].toString());
            if (e["estado_archivo"].toString() != QLatin1String("ok")) Store::exec(QStringLiteral("UPDATE extras SET estado_archivo = 'ok' WHERE id = ?"), {e["id"]});
            return false;
        }
        const QString h = hashOf(a);
        for (const QVariantMap &c : extrasExisting) {
            if (c["media_item_id"].toString() != mediaItemId || extrasSeen.contains(c["id"].toString()) || c["hash_respaldo"].isNull() || c["hash_respaldo"].toString() != h) continue;
            extrasSeen.insert(c["id"].toString());
            Store::exec(QStringLiteral("UPDATE extras SET ruta_relativa = ?, estado_archivo = 'ok', hash_respaldo = ? WHERE id = ?"), {a.rel, h, c["id"]});
            return false;
        }
        // Título derivado SOLO del nombre de archivo: si viniera de una carpeta «extras» todos se llamarían «Extras».
        const QString title = Naming::parseTitle(Naming::baseName(a.rel)).title;
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        Store::exec(QStringLiteral("INSERT INTO extras (id, media_item_id, ruta_relativa, hash_respaldo, estado_archivo, titulo, creado_en) VALUES (?, ?, ?, ?, 'ok', ?, ?)"), {id, mediaItemId, a.rel, h, title, now});
        extrasSeen.insert(id);
        return true;
    };

    for (const MoviePlan &p : std::as_const(movies)) {
        bool isNew;
        QString byHash;
        reconcileItem(p.official, {nullVar(), nullVar(), nullVar()}, &isNew, &byHash);
        if (isNew) r.nuevos++;
        else { r.reconciliados++; if (!byHash.isEmpty()) r.reconciliadosPorHashIds << byHash; }
        step(p.official.rel);
        const QString officialId = Store::scalar(QStringLiteral("SELECT id FROM media_items WHERE library_id = ? AND ruta_relativa = ?"), {libId, p.official.rel}).toString();
        for (const FileEntry &x : p.extras) {
            if (!officialId.isEmpty()) { if (reconcileExtra(officialId, x)) r.extrasNuevos++; else r.extrasReconciliados++; }
            step(x.rel);
        }
    }
    for (const SeriesPlan &p : std::as_const(seriesPlans)) {
        QString serieId;
        bool found = false;
        for (const QVariantMap &s : seriesExisting) {
            if (s["ruta_relativa"].toString() != p.folder) continue;
            serieId = s["id"].toString();
            seriesSeen.insert(serieId);
            if (s["estado_archivo"].toString() != QLatin1String("ok")) Store::exec(QStringLiteral("UPDATE series SET estado_archivo = 'ok', actualizado_en = ? WHERE id = ?"), {now, serieId});
            r.seriesReconciliadas++;
            found = true;
            break;
        }
        if (!found) {
            serieId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            Store::exec(QStringLiteral("INSERT INTO series (id, library_id, ruta_relativa, estado_archivo, creado_en, actualizado_en) VALUES (?, ?, ?, 'ok', ?, ?)"), {serieId, libId, p.folder, now, now});
            seriesSeen.insert(serieId);
            r.seriesNuevas++;
        }
        for (const EpisodePlan &ep : p.episodes) {
            bool isNew;
            QString byHash;
            reconcileItem(ep.file, {serieId, ep.season, ep.episode}, &isNew, &byHash);
            if (isNew) r.nuevos++;
            else { r.reconciliados++; if (!byHash.isEmpty()) r.reconciliadosPorHashIds << byHash; }
            step(ep.file.rel);
        }
    }

    // Solo si el escaneo llegó al final: lo que no se vio queda «no encontrado» (nunca se borra solo).
    progress({QStringLiteral("cerrando"), total, total, {}});
    for (const QVariantMap &m : existing) {
        if (seen.contains(m["id"].toString())) continue;
        if (m["estado_archivo"].toString() != QLatin1String("noEncontrado")) Store::exec(QStringLiteral("UPDATE media_items SET estado_archivo = 'noEncontrado', actualizado_en = ? WHERE id = ?"), {now, m["id"]});
        r.noEncontrados++;
    }
    if (lib.type == QLatin1String("serie")) {
        for (const QVariantMap &s : seriesExisting) {
            if (seriesSeen.contains(s["id"].toString())) continue;
            if (s["estado_archivo"].toString() != QLatin1String("noEncontrado")) Store::exec(QStringLiteral("UPDATE series SET estado_archivo = 'noEncontrado', actualizado_en = ? WHERE id = ?"), {now, s["id"]});
            r.seriesNoEncontradas++;
        }
    } else {
        for (const QVariantMap &e : extrasExisting) {
            if (extrasSeen.contains(e["id"].toString())) continue;
            if (e["estado_archivo"].toString() != QLatin1String("noEncontrado")) Store::exec(QStringLiteral("UPDATE extras SET estado_archivo = 'noEncontrado' WHERE id = ?"), {e["id"]});
        }
    }
    return r;
}

} // namespace Scanner
