#include "MovieModel.h"

#include <QDir>
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
    if (row < 0 || row >= m_view.size()) return {};
    const Movie &m = m_view.at(row);
    auto list = [](const QString &json) {
        QVariantList out;
        for (const auto &v : QJsonDocument::fromJson(json.toUtf8()).array()) out << v.toObject().toVariantMap();
        return out;
    };
    return {{"id", m.id}, {"title", m.title}, {"year", m.year}, {"path", m.path}, {"posterUrl", m.posterUrl},
            {"backdropUrl", m.backdropUrl}, {"origTitle", m.origTitle}, {"studio", m.studio}, {"lang", m.lang},
            {"duration", m.duration}, {"genres", m.genres}, {"synopsis", m.synopsis}, {"cast", list(m.castJson)},
            {"crew", list(m.crewJson)}, {"progressPos", m.progressPos}, {"progressState", m.progressState}};
}

bool MovieModel::fileExists(const QString &path) const { return QFileInfo::exists(path); }

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
        if (!QFileInfo::exists(m_dbPath)) {
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
                "       (SELECT estado FROM watch_progresses w WHERE w.media_item_id = m.id ORDER BY ultima_reproduccion DESC LIMIT 1) "
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
                    m.path = QDir::toNativeSeparators(q.value(7).toString() + QLatin1Char('/') + q.value(8).toString());
                    m.origTitle = q.value(9).toString();
                    m.backdropUrl = q.value(10).toString();
                    m.studio = q.value(11).toString();
                    m.lang = q.value(12).toString();
                    m.castJson = q.value(13).toString();
                    m.crewJson = q.value(14).toString();
                    m.progressPos = q.value(15).toDouble();
                    m.progressState = q.value(16).toString();
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
    endResetModel();
    emit errorChanged();
    emit countChanged();
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
    endResetModel();
    emit countChanged();
}

int MovieModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_view.size()); }

QVariant MovieModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_view.size()) return {};
    const Movie &m = m_view.at(index.row());
    switch (role) {
    case IdRole: return m.id;
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
    return {{IdRole, "movieId"}, {TitleRole, "title"}, {YearRole, "year"}, {PosterRole, "posterUrl"},
            {DurationRole, "duration"}, {GenresRole, "genres"}, {PathRole, "path"}, {SynopsisRole, "synopsis"}};
}
