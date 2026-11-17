#pragma once
#include <QAbstractListModel>
#include <QQmlEngine>
#include <QString>
#include <QVector>

/** Catálogo de series (solo lectura salvo el progreso): series → temporadas → episodios, desde una copia de cinemalove.sqlite. */
class SeriesCatalog : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString dbPath READ dbPath WRITE setDbPath NOTIFY dbPathChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pagingChanged)          // página actual (0 = primera)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY pagingChanged)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY pagingChanged)           // elementos que cumplen el filtro (todas las páginas)
    Q_PROPERTY(int pageSize READ pageSize CONSTANT)
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    enum Roles { IdRole = Qt::UserRole + 1, ProgressStateRole, ProgressFractionRole, TitleRole, YearRole, PosterRole, SubtitleRole };

    explicit SeriesCatalog(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString dbPath() const { return m_dbPath; }
    void setDbPath(const QString &p);
    QString filter() const { return m_filter; }
    void setFilter(const QString &f);
    int total() const { return m_all.size(); }
    int matchCount() const { return int(m_view.size()); }
    static constexpr int kPageSize = 100;   // elementos por página
    int pageSize() const { return kPageSize; }
    int page() const { return m_page; }
    int pageCount() const { return qMax(1, (matchCount() + kPageSize - 1) / kPageSize); }
    void setPage(int p);

    QString error() const { return m_error; }

    Q_INVOKABLE void reload() { load(); if (!m_filter.isEmpty()) applyFilter(); }

    /** Ficha completa de una serie: metadatos, temporadas con conteos, estadísticas y el episodio sugerido (`next`). */
    Q_INVOKABLE QVariantMap detail(const QString &seriesId) const;
    Q_INVOKABLE QVariantMap detailAt(int row) const;
    Q_INVOKABLE QVariantList episodes(const QString &seriesId, int season) const;
    Q_INVOKABLE QVariantMap episode(const QString &episodeId) const;
    /** Id del episodio siguiente (mismo orden temporada/episodio) o "" si es el último. */
    Q_INVOKABLE QString nextEpisodeId(const QString &episodeId) const;
    /** Tarjetas de «Continuar viendo»: por serie, el episodio en curso más reciente. */
    Q_INVOKABLE QVariantList continueItems(int limit = 20) const;
    Q_INVOKABLE bool saveProgress(const QString &episodeId, double position, const QString &state,
                                  const QString &audioId = {}, const QString &subId = {});

signals:
    void dbPathChanged();
    void filterChanged();
    void countChanged();
    void pagingChanged();
    void errorChanged();
    void homeChanged();

private:
    struct Episode {
        QString id, title, synopsis, stillUrl, path, progressState, progressAudio, progressSub;
        int season = 0, number = 0, duration = 0;
        double progressPos = 0;
        qint64 lastPlayed = 0;
    };
    struct Season { int number = 0; QString posterUrl; };
    struct Series {
        QString id, title, origTitle, synopsis, studio, lang, posterUrl, backdropUrl, genres, castJson, crewJson, folded;
        int year = 0;
        QVector<Season> seasons;
        QVector<Episode> episodes; // ordenados por temporada y número
        int watched() const;
        qint64 lastPlayed() const;
    };

    void load();
    void applyFilter();
    static QString fold(const QString &s);
    const Series *seriesById(const QString &id) const;
    QVariantMap episodeMap(const Series &s, const Episode &e) const;
    QVariantMap nextFor(const Series &s) const;
    void refreshSeries(const QString &id);

    QString m_dbPath, m_filter, m_error;
    QVector<Series> m_all;
    QVector<int> m_view; // índices en m_all
    int m_page = 0;
    int offset() const { return m_page * kPageSize; }
};
