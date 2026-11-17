#include "SeriesCatalog.h"
#include "ProgressWriter.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <algorithm>
#include <cmath>

SeriesCatalog::SeriesCatalog(QObject *parent) : QAbstractListModel(parent) {}

QString SeriesCatalog::fold(const QString &s)
{
    const QString d = s.normalized(QString::NormalizationForm_D);
    QString out;
    for (const QChar c : d)
        if (c.category() != QChar::Mark_NonSpacing) out += c.toLower();
    return out;
}

int SeriesCatalog::Series::watched() const
{
    int n = 0;
    for (const Episode &e : episodes) n += e.progressState == QLatin1String("visto");
    return n;
}

qint64 SeriesCatalog::Series::lastPlayed() const
{
    qint64 t = 0;
    for (const Episode &e : episodes) t = std::max(t, e.lastPlayed);
    return t;
}

void SeriesCatalog::setDbPath(const QString &p)
{
    if (p == m_dbPath) return;
    m_dbPath = p;
    emit dbPathChanged();
    load();
}

void SeriesCatalog::setFilter(const QString &f)
{
    if (f == m_filter) return;
    m_filter = f;
    emit filterChanged();
    applyFilter();
}

void SeriesCatalog::load()
{
    beginResetModel();
    m_all.clear();
    m_view.clear();
    m_error.clear();

    const QString conn = QStringLiteral("series-") + QUuid::createUuid().toString(QUuid::Id128);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(m_dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (m_dbPath.isEmpty()) {
            // aún no hay colección abierta
        } else if (!QFileInfo::exists(m_dbPath)) {
            m_error = tr("No se encuentra la base de datos: %1").arg(m_dbPath);
        } else if (!db.open()) {
            m_error = db.lastError().text();
        } else {
            QHash<QString, int> index; // id de serie → posición en m_all
            QSqlQuery q(db);
            q.setForwardOnly(true);
            if (!q.exec(QStringLiteral("SELECT id, titulo, titulo_original, anio, sinopsis, estudio, idioma_original, poster_url, backdrop_url, "
                                       "generos, reparto, equipo, ruta_relativa FROM series WHERE estado_archivo = 'ok'"))) {
                m_error = q.lastError().text();
            } else {
                while (q.next()) {
                    Series s;
                    s.id = q.value(0).toString();
                    s.title = q.value(1).toString();
                    s.origTitle = q.value(2).toString();
                    s.year = q.value(3).toInt();
                    s.synopsis = q.value(4).toString();
                    s.studio = q.value(5).toString();
                    s.lang = q.value(6).toString();
                    s.posterUrl = q.value(7).toString();
                    s.backdropUrl = q.value(8).toString();
                    QStringList gl;
                    for (const auto &v : QJsonDocument::fromJson(q.value(9).toString().toUtf8()).array()) gl << v.toString();
                    s.genres = gl.join(QStringLiteral(" · "));
                    s.castJson = q.value(10).toString();
                    s.crewJson = q.value(11).toString();
                    if (s.title.trimmed().isEmpty()) s.title = q.value(12).toString();
                    s.folded = fold(s.title);
                    index.insert(s.id, m_all.size());
                    m_all.append(s);
                }
            }

            QHash<QString, QString> seasonPoster; // «serieId#n» → póster
            QSqlQuery sq(db);
            if (sq.exec(QStringLiteral("SELECT series_id, numero_temporada, poster_url FROM seasons")))
                while (sq.next()) seasonPoster.insert(sq.value(0).toString() + QLatin1Char('#') + sq.value(1).toString(), sq.value(2).toString());

            QSqlQuery eq(db);
            eq.setForwardOnly(true);
            const QString progress = QStringLiteral("(SELECT %1 FROM watch_progresses w WHERE w.media_item_id = m.id ORDER BY ultima_reproduccion DESC LIMIT 1)");
            const bool ok = eq.exec(QStringLiteral(
                "SELECT m.id, m.series_id, m.numero_temporada, m.numero_episodio, m.titulo, m.sinopsis, m.poster_url, m.duracion_segundos, "
                "       l.ruta_actual, m.ruta_relativa, ")
                + progress.arg(QStringLiteral("posicion_segundos")) + QStringLiteral(", ") + progress.arg(QStringLiteral("estado")) + QStringLiteral(", ")
                + progress.arg(QStringLiteral("pista_audio_id")) + QStringLiteral(", ") + progress.arg(QStringLiteral("pista_subtitulo_id")) + QStringLiteral(", ")
                + progress.arg(QStringLiteral("ultima_reproduccion"))
                + QStringLiteral(" FROM media_items m JOIN libraries l ON l.id = m.library_id "
                                 "WHERE m.series_id IS NOT NULL AND m.estado_archivo = 'ok'"));
            if (!ok) {
                m_error = eq.lastError().text();
            } else {
                while (eq.next()) {
                    const auto it = index.constFind(eq.value(1).toString());
                    if (it == index.constEnd()) continue;
                    Series &s = m_all[*it];
                    Episode e;
                    e.id = eq.value(0).toString();
                    e.season = eq.value(2).toInt();
                    e.number = eq.value(3).toInt();
                    e.title = eq.value(4).toString();
                    e.synopsis = eq.value(5).toString();
                    e.stillUrl = eq.value(6).toString();
                    e.duration = eq.value(7).toInt();
                    e.path = QDir::toNativeSeparators(eq.value(8).toString() + QLatin1Char('/') + eq.value(9).toString());
                    e.progressPos = eq.value(10).toDouble();
                    e.progressState = eq.value(11).toString();
                    e.progressAudio = eq.value(12).toString();
                    e.progressSub = eq.value(13).toString();
                    e.lastPlayed = eq.value(14).toLongLong();
                    s.episodes.append(e);
                }
            }
            db.close();

            for (Series &s : m_all) {
                std::sort(s.episodes.begin(), s.episodes.end(), [](const Episode &a, const Episode &b) {
                    return a.season != b.season ? a.season < b.season : a.number < b.number;
                });
                int last = -1;
                for (const Episode &e : std::as_const(s.episodes)) {
                    if (e.season == last) continue;
                    last = e.season;
                    Season se;
                    se.number = e.season;
                    se.posterUrl = seasonPoster.value(s.id + QLatin1Char('#') + QString::number(e.season), s.posterUrl);
                    s.seasons.append(se);
                }
            }
            // Series sin episodios disponibles no se listan (nada que reproducir).
            m_all.erase(std::remove_if(m_all.begin(), m_all.end(), [](const Series &s) { return s.episodes.isEmpty(); }), m_all.end());
        }
    }
    QSqlDatabase::removeDatabase(conn);

    std::sort(m_all.begin(), m_all.end(), [](const Series &a, const Series &b) { return QString::localeAwareCompare(a.title, b.title) < 0; });
    for (int i = 0; i < m_all.size(); ++i) m_view.append(i);
    endResetModel();
    emit errorChanged();
    emit countChanged();
}

void SeriesCatalog::applyFilter()
{
    beginResetModel();
    const QString f = fold(m_filter.trimmed());
    m_view.clear();
    for (int i = 0; i < m_all.size(); ++i)
        if (f.isEmpty() || m_all.at(i).folded.contains(f)) m_view.append(i);
    endResetModel();
    emit countChanged();
}

int SeriesCatalog::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_view.size()); }

QVariant SeriesCatalog::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_view.size()) return {};
    const Series &s = m_all.at(m_view.at(index.row()));
    const int total = int(s.episodes.size()), seen = s.watched();
    switch (role) {
    case IdRole: return s.id;
    case TitleRole: return s.title;
    case YearRole: return s.year;
    case PosterRole: return s.posterUrl;
    case ProgressStateRole: return seen == total ? QStringLiteral("visto") : (seen > 0 ? QStringLiteral("enProgreso") : QString());
    case ProgressFractionRole: return total > 0 ? double(seen) / total : 0.0;
    case SubtitleRole: {
        const int n = int(s.seasons.size());
        const QString t = n == 1 ? tr("1 temporada") : tr("%1 temporadas").arg(n);
        return s.year > 0 ? QStringLiteral("%1 · %2").arg(s.year).arg(t) : t;
    }
    }
    return {};
}

QHash<int, QByteArray> SeriesCatalog::roleNames() const
{
    return {{IdRole, "movieId"}, {ProgressStateRole, "progressState"}, {ProgressFractionRole, "progressFraction"}, {TitleRole, "title"},
            {YearRole, "year"}, {PosterRole, "posterUrl"}, {SubtitleRole, "subtitle"}};
}

const SeriesCatalog::Series *SeriesCatalog::seriesById(const QString &id) const
{
    for (const Series &s : m_all)
        if (s.id == id) return &s;
    return nullptr;
}

QVariantMap SeriesCatalog::episodeMap(const Series &s, const Episode &e) const
{
    const QString title = e.title.trimmed().isEmpty() ? tr("Episodio %1").arg(e.number) : e.title;
    return {{"id", e.id}, {"kind", "episode"}, {"seriesId", s.id}, {"seriesTitle", s.title}, {"season", e.season}, {"number", e.number},
            {"label", QStringLiteral("T%1 · E%2").arg(e.season).arg(e.number)}, {"title", title}, {"synopsis", e.synopsis},
            {"stillUrl", e.stillUrl}, {"duration", e.duration}, {"path", e.path}, {"progressState", e.progressState},
            {"progressPos", e.progressPos}, {"progressAudio", e.progressAudio}, {"progressSub", e.progressSub},
            {"progressFraction", e.duration > 0 ? qBound(0.0, e.progressPos / e.duration, 1.0) : 0.0}};
}

// Episodio sugerido: el que está en curso (más reciente) → el siguiente al último visto → el primero → «volver a ver».
QVariantMap SeriesCatalog::nextFor(const Series &s) const
{
    const Episode *resume = nullptr;
    for (const Episode &e : s.episodes)
        if (e.progressState == QLatin1String("enProgreso") && e.progressPos > 0 && (!resume || e.lastPlayed > resume->lastPlayed)) resume = &e;
    if (resume) return {{"mode", "resume"}, {"episode", episodeMap(s, *resume)}};

    int lastSeen = -1;
    qint64 best = -1;
    for (int i = 0; i < s.episodes.size(); ++i)
        if (s.episodes.at(i).progressState == QLatin1String("visto") && s.episodes.at(i).lastPlayed >= best) { best = s.episodes.at(i).lastPlayed; lastSeen = i; }
    if (lastSeen < 0) return {{"mode", "start"}, {"episode", episodeMap(s, s.episodes.first())}};
    for (int i = lastSeen + 1; i < s.episodes.size(); ++i)
        if (s.episodes.at(i).progressState != QLatin1String("visto")) return {{"mode", "next"}, {"episode", episodeMap(s, s.episodes.at(i))}};
    for (int i = 0; i < lastSeen; ++i) // los de antes del último visto que quedaron sin ver
        if (s.episodes.at(i).progressState != QLatin1String("visto")) return {{"mode", "next"}, {"episode", episodeMap(s, s.episodes.at(i))}};
    return {{"mode", "rewatch"}, {"episode", episodeMap(s, s.episodes.first())}};
}

QVariantMap SeriesCatalog::detail(const QString &seriesId) const
{
    const Series *sp = seriesById(seriesId);
    if (!sp) return {};
    const Series &s = *sp;
    auto list = [](const QString &json) {
        QVariantList out;
        for (const auto &v : QJsonDocument::fromJson(json.toUtf8()).array()) out << v.toObject().toVariantMap();
        return out;
    };
    QVariantList seasons;
    for (const Season &se : s.seasons) {
        int n = 0, seen = 0;
        for (const Episode &e : s.episodes)
            if (e.season == se.number) { ++n; seen += e.progressState == QLatin1String("visto"); }
        seasons << QVariantMap{{"number", se.number}, {"posterUrl", se.posterUrl}, {"episodes", n}, {"watched", seen},
                               {"progressFraction", n > 0 ? double(seen) / n : 0.0}};
    }
    return {{"id", s.id}, {"kind", "series"}, {"title", s.title}, {"origTitle", s.origTitle}, {"year", s.year}, {"synopsis", s.synopsis},
            {"studio", s.studio}, {"lang", s.lang}, {"posterUrl", s.posterUrl}, {"backdropUrl", s.backdropUrl}, {"genres", s.genres},
            {"cast", list(s.castJson)}, {"crew", list(s.crewJson)}, {"seasons", seasons}, {"episodeCount", int(s.episodes.size())},
            {"watchedCount", s.watched()}, {"next", nextFor(s)}};
}

QVariantMap SeriesCatalog::detailAt(int row) const
{
    return (row < 0 || row >= m_view.size()) ? QVariantMap{} : detail(m_all.at(m_view.at(row)).id);
}

QVariantList SeriesCatalog::episodes(const QString &seriesId, int season) const
{
    QVariantList out;
    if (const Series *s = seriesById(seriesId))
        for (const Episode &e : s->episodes)
            if (e.season == season) out << episodeMap(*s, e);
    return out;
}

QVariantMap SeriesCatalog::episode(const QString &episodeId) const
{
    for (const Series &s : m_all)
        for (const Episode &e : s.episodes)
            if (e.id == episodeId) return episodeMap(s, e);
    return {};
}

QString SeriesCatalog::nextEpisodeId(const QString &episodeId) const
{
    for (const Series &s : m_all)
        for (int i = 0; i < s.episodes.size(); ++i)
            if (s.episodes.at(i).id == episodeId) return i + 1 < s.episodes.size() ? s.episodes.at(i + 1).id : QString();
    return {};
}

QVariantList SeriesCatalog::continueItems(int limit) const
{
    struct Item { qint64 t; QVariantMap m; };
    QVector<Item> items;
    for (const Series &s : m_all) {
        const Episode *best = nullptr;
        for (const Episode &e : s.episodes)
            if (e.progressState == QLatin1String("enProgreso") && e.progressPos > 0 && (e.duration <= 0 || e.progressPos / e.duration < 0.95)
                && (!best || e.lastPlayed > best->lastPlayed)) best = &e;
        if (!best) continue;
        QVariantMap m = episodeMap(s, *best);
        m.insert("title", s.title); // la tarjeta lleva el nombre de la serie y, debajo, «T1 · E3»
        m.insert("subtitle", m.value("label"));
        m.insert("episodeTitle", episodeMap(s, *best).value("title"));
        m.insert("posterUrl", s.posterUrl);
        m.insert("backdropUrl", s.backdropUrl.isEmpty() ? best->stillUrl : s.backdropUrl);
        m.insert("genres", s.genres);
        m.insert("year", s.year);
        m.insert("lastPlayed", best->lastPlayed);
        if (m.value("synopsis").toString().isEmpty()) m.insert("synopsis", s.synopsis);
        items.append({best->lastPlayed, m});
    }
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.t > b.t; });
    QVariantList out;
    for (const Item &i : std::as_const(items)) {
        if (out.size() >= limit) break;
        out << i.m;
    }
    return out;
}

bool SeriesCatalog::saveProgress(const QString &episodeId, double position, const QString &state, const QString &audioId, const QString &subId)
{
    const bool watched = state == QLatin1String("visto");
    const qint64 pos = watched ? 0 : qint64(std::llround(position));
    if (!ProgressWriter::save(m_dbPath, episodeId, pos, state, audioId, subId)) return false;
    QString seriesId;
    for (Series &s : m_all)
        for (Episode &e : s.episodes)
            if (e.id == episodeId) {
                e.progressState = state;
                e.progressPos = double(pos);
                e.lastPlayed = QDateTime::currentSecsSinceEpoch();
                if (!audioId.isEmpty()) e.progressAudio = audioId;
                if (!subId.isEmpty()) e.progressSub = subId;
                seriesId = s.id;
            }
    if (!seriesId.isEmpty()) refreshSeries(seriesId);
    emit homeChanged();
    return true;
}

void SeriesCatalog::refreshSeries(const QString &id)
{
    for (int r = 0; r < m_view.size(); ++r)
        if (m_all.at(m_view.at(r)).id == id) {
            const QModelIndex ix = index(r);
            emit dataChanged(ix, ix, {ProgressStateRole, ProgressFractionRole});
        }
}
