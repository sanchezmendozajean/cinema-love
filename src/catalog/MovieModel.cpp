#include "MovieModel.h"
#include "ProgressWriter.h"

#include <QDateTime>
#include <QDir>
#include <cmath>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <algorithm>

MovieModel::MovieModel(QObject *parent) : QAbstractListModel(parent) {}

QString MovieModel::fold(const QString &s)
{
    // Descompone (NFD) y quita las marcas diacríticas: «Dálmatas» ↔ «dalmatas».
    QString d = s.normalized(QString::NormalizationForm_D);
    QString out;
    for (const QChar c : d)
        if (c.category() != QChar::Mark_NonSpacing) out += c.toLower();
    return out;
}

void MovieModel::setDbPath(const QString &p)
{
    if (p == m_dbPath) return;
    m_dbPath = p;
    emit dbPathChanged();
    load();
}

void MovieModel::setFilter(const QString &f)
{
    if (f == m_filter) return;
    m_filter = f;
    emit filterChanged();
    applyFilter();
}

QVariantMap MovieModel::get(int row) const
{
    const int vi = offset() + row;   // `row` es relativa a la página visible
    if (row < 0 || vi >= m_view.size()) return {};
    return toMap(m_view.at(vi));
}

QVariantMap MovieModel::getById(const QString &id) const
{
    for (const Movie &m : m_all)
        if (m.id == id) return toMap(m);
    return {};
}

QVariantMap MovieModel::toMap(const Movie &m) const
{
    auto list = [](const QString &json) {
        QVariantList out;
        for (const auto &v : QJsonDocument::fromJson(json.toUtf8()).array()) out << v.toObject().toVariantMap();
        return out;
    };
    return {{"id", m.id}, {"title", m.title}, {"year", m.year}, {"path", m.path}, {"posterUrl", m.posterUrl},
            {"backdropUrl", m.backdropUrl}, {"origTitle", m.origTitle}, {"studio", m.studio}, {"lang", m.lang},
            {"duration", m.duration}, {"genres", m.genres}, {"synopsis", m.synopsis}, {"cast", list(m.castJson)},
            {"crew", list(m.crewJson)}, {"progressPos", m.progressPos}, {"progressState", m.progressState},
            {"progressAudio", m.progressAudio}, {"progressSub", m.progressSub}};
}

// Regla de oro de los datos: solo se escribe en copias de desarrollo. Escribir en la base real exigirá una decisión explícita.
bool MovieModel::writable() const
{
    return ProgressWriter::allowed(m_dbPath);
}

bool MovieModel::saveProgress(const QString &id, double position, const QString &state, const QString &audioId, const QString &subId)
{
    if (!writable()) return false;
    const bool watched = state == QLatin1String("visto");
    const qint64 pos = watched ? 0 : qint64(std::llround(position));
    const bool ok = ProgressWriter::save(m_dbPath, id, pos, state, audioId, subId);

    if (ok) {
        for (Movie &m : m_all) {
            if (m.id != id) continue;
            m.progressState = state;
            m.progressPos = double(pos);
            m.lastPlayed = QDateTime::currentSecsSinceEpoch();
            if (watched) m.queuedAt = 0;
            if (!audioId.isEmpty()) m.progressAudio = audioId;
            if (!subId.isEmpty()) m.progressSub = subId;
        }
        for (Movie &m : m_view) {
            if (m.id != id) continue;
            m.progressState = state; m.progressPos = double(pos);
            if (!audioId.isEmpty()) m.progressAudio = audioId;
            if (!subId.isEmpty()) m.progressSub = subId;
        }
        refreshRow(id);
        emit homeChanged();
    }
    return ok;
}

QVariantMap MovieModel::card(const Movie &m) const
{
    const double frac = m.duration > 0 ? qBound(0.0, m.progressPos / m.duration, 1.0) : 0.0;
    return {{"id", m.id}, {"title", m.title}, {"year", m.year}, {"posterUrl", m.posterUrl}, {"backdropUrl", m.backdropUrl},
            {"progressState", m.progressState}, {"progressFraction", frac}, {"duration", m.duration},
            {"progressPos", m.progressPos}, {"genres", m.genres}, {"synopsis", m.synopsis}, {"lastPlayed", m.lastPlayed}, {"kind", "movie"}};
}

QVariantList MovieModel::homeRow(const QString &kind, int limit) const
{
    QVector<const Movie *> sel;
    for (const Movie &m : m_all) {
        if (kind == QLatin1String("continue")) {
            if (m.progressState == QLatin1String("enProgreso") && m.progressPos > 0 && (m.duration <= 0 || m.progressPos / m.duration < 0.95)) sel << &m;
        } else if (kind == QLatin1String("queue")) {
            if (m.queuedAt > 0) sel << &m;
        } else if (kind == QLatin1String("watched")) {
            if (m.progressState == QLatin1String("visto")) sel << &m;
        } else {
            sel << &m; // recent
        }
    }
    auto key = [&](const Movie *m) {
        if (kind == QLatin1String("recent")) return m->createdAt;
        if (kind == QLatin1String("queue")) return m->queuedAt;
        return m->lastPlayed;
    };
    std::stable_sort(sel.begin(), sel.end(), [&](const Movie *a, const Movie *b) { return key(a) > key(b); });
    QVariantList out;
    for (const Movie *m : std::as_const(sel)) {
        if (out.size() >= limit) break;
        out << card(*m);
    }
    return out;
}

void MovieModel::refreshRow(const QString &id)
{
    for (int i = 0; i < m_view.size(); ++i)
        if (m_view.at(i).id == id) {
            const int local = i - offset();
            if (local < 0 || local >= rowCount()) continue;   // no está en la página visible
            const QModelIndex ix = index(local);
            emit dataChanged(ix, ix, {ProgressStateRole, ProgressFractionRole});
        }
}

bool MovieModel::fileExists(const QString &path) const { return path.startsWith(QLatin1String("http://")) || path.startsWith(QLatin1String("https://")) || QFileInfo::exists(path); }   // Modo Cliente: la «ruta» es la URL de streaming

void MovieModel::load()
{
    beginResetModel();
    m_all.clear();
    m_view.clear();
    m_error.clear();

    const QString conn = QStringLiteral("movies-") + QUuid::createUuid().toString(QUuid::Id128);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(m_dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY")); // regla de oro de los datos: nunca escribir
        if (m_dbPath.isEmpty()) {
            // aún no hay colección abierta (asistente de bienvenida): lista vacía, sin error
        } else if (!QFileInfo::exists(m_dbPath)) {
            m_error = tr("No se encuentra la base de datos: %1").arg(m_dbPath);
        } else if (!db.open()) {
            m_error = db.lastError().text();
        } else {
            QSqlQuery q(db);
            q.setForwardOnly(true);
            const bool ok = q.exec(QStringLiteral(
                "SELECT m.id, m.titulo, m.anio, m.poster_url, m.duracion_segundos, m.generos, m.sinopsis, "
                "       l.ruta_actual, m.ruta_relativa, m.titulo_original, m.backdrop_url, m.estudio, m.idioma_original, "
                "       m.reparto, m.equipo, "
                "       (SELECT posicion_segundos FROM watch_progresses w WHERE w.media_item_id = m.id ORDER BY ultima_reproduccion DESC LIMIT 1), "
                "       (SELECT estado FROM watch_progresses w WHERE w.media_item_id = m.id ORDER BY ultima_reproduccion DESC LIMIT 1), "
                "       (SELECT pista_audio_id FROM watch_progresses w WHERE w.media_item_id = m.id ORDER BY ultima_reproduccion DESC LIMIT 1), "
                "       (SELECT pista_subtitulo_id FROM watch_progresses w WHERE w.media_item_id = m.id ORDER BY ultima_reproduccion DESC LIMIT 1), "
                "       m.creado_en, "
                "       (SELECT ultima_reproduccion FROM watch_progresses w WHERE w.media_item_id = m.id ORDER BY ultima_reproduccion DESC LIMIT 1), "
                "       (SELECT agregado_en FROM proximas_a_ver p WHERE p.media_item_id = m.id AND p.usuario_id IS NULL LIMIT 1) "
                "FROM media_items m JOIN libraries l ON l.id = m.library_id "
                "WHERE m.series_id IS NULL AND l.tipo = 'pelicula' AND m.estado_archivo = 'ok'"));
            if (!ok) {
                m_error = q.lastError().text();
            } else {
                while (q.next()) {
                    Movie m;
                    m.id = q.value(0).toString();
                    m.title = q.value(1).toString();
                    m.year = q.value(2).toInt();
                    m.posterUrl = q.value(3).toString();
                    m.duration = q.value(4).toInt();
                    const auto g = QJsonDocument::fromJson(q.value(5).toString().toUtf8()).array();
                    QStringList gl;
                    for (const auto &v : g) gl << v.toString();
                    m.genres = gl.join(QStringLiteral(" · "));
                    m.synopsis = q.value(6).toString();
                    m.path = q.value(7).toString().startsWith(QLatin1String("http")) ? q.value(7).toString() + QLatin1Char('/') + q.value(8).toString()   // Modo Cliente: URL de streaming
                                                                     : QDir::toNativeSeparators(q.value(7).toString() + QLatin1Char('/') + q.value(8).toString());
                    m.origTitle = q.value(9).toString();
                    m.backdropUrl = q.value(10).toString();
                    m.studio = q.value(11).toString();
                    m.lang = q.value(12).toString();
                    m.castJson = q.value(13).toString();
                    m.crewJson = q.value(14).toString();
                    m.progressPos = q.value(15).toDouble();
                    m.progressState = q.value(16).toString();
                    m.progressAudio = q.value(17).toString();
                    m.progressSub = q.value(18).toString();
                    m.createdAt = q.value(19).toLongLong();
                    m.lastPlayed = q.value(20).toLongLong();
                    m.queuedAt = q.value(21).toLongLong();
                    if (m.title.trimmed().isEmpty()) {
                        // Sin metadata de TMDb: se usa el nombre de la carpeta («Título (2001)») para que no quede una tarjeta vacía.
                        const QString rel = q.value(8).toString();
                        const QStringList parts = rel.split(QLatin1Char('/'));
                        QString name = parts.size() > 1 ? parts.at(parts.size() - 2) : QFileInfo(rel).completeBaseName();
                        static const QRegularExpression yearRx(QStringLiteral(R"(\s*\((\d{4})\)\s*$)"));
                        const auto mt = yearRx.match(name);
                        if (mt.hasMatch()) {
                            if (m.year == 0) m.year = mt.captured(1).toInt();
                            name.truncate(mt.capturedStart());
                        }
                        m.title = name.trimmed();
                    }
                    m.folded = fold(m.title);
                    m_all.append(m);
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(conn);

    std::sort(m_all.begin(), m_all.end(), [](const Movie &a, const Movie &b) {
        return QString::localeAwareCompare(a.title, b.title) < 0;
    });
    m_view = m_all;
    m_page = qMin(m_page, pageCount() - 1);   // tras recargar se conserva la página si sigue existiendo
    endResetModel();
    emit errorChanged();
    emit countChanged();
    emit pagingChanged();
}

void MovieModel::applyFilter()
{
    beginResetModel();
    const QString f = fold(m_filter.trimmed());
    if (f.isEmpty()) {
        m_view = m_all;
    } else {
        m_view.clear();
        for (const Movie &m : std::as_const(m_all))
            if (m.folded.contains(f)) m_view.append(m);
    }
    m_page = 0;   // un filtro nuevo vuelve a la primera página
    endResetModel();
    emit countChanged();
    emit pagingChanged();
}

void MovieModel::setPage(int p)
{
    p = qBound(0, p, pageCount() - 1);
    if (p == m_page) return;
    beginResetModel();
    m_page = p;
    endResetModel();
    emit pagingChanged();
}

int MovieModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : qBound(0, int(m_view.size()) - offset(), kPageSize); }

QVariant MovieModel::data(const QModelIndex &index, int role) const
{
    const int vi = offset() + index.row();
    if (!index.isValid() || vi >= m_view.size()) return {};
    const Movie &m = m_view.at(vi);
    switch (role) {
    case SubtitleRole: return QString();
    case IdRole: return m.id;
    case ProgressStateRole: return m.progressState;
    case ProgressFractionRole: return m.duration > 0 ? qBound(0.0, m.progressPos / m.duration, 1.0) : 0.0;
    case TitleRole: return m.title;
    case YearRole: return m.year;
    case PosterRole: return m.posterUrl;
    case DurationRole: return m.duration;
    case GenresRole: return m.genres;
    case PathRole: return m.path;
    case SynopsisRole: return m.synopsis;
    }
    return {};
}

QHash<int, QByteArray> MovieModel::roleNames() const
{
    return {{IdRole, "movieId"}, {ProgressStateRole, "progressState"}, {ProgressFractionRole, "progressFraction"}, {SubtitleRole, "subtitle"}, {TitleRole, "title"}, {YearRole, "year"}, {PosterRole, "posterUrl"},
            {DurationRole, "duration"}, {GenresRole, "genres"}, {PathRole, "path"}, {SynopsisRole, "synopsis"}};
}
