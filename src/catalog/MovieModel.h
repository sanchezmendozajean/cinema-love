#pragma once
#include <QAbstractListModel>
#include <QQmlEngine>
#include <QString>
#include <QVector>

/** Catálogo de películas (solo lectura) leído de una copia de cinemalove.sqlite. Spike de la Fase 2. */
class MovieModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString dbPath READ dbPath WRITE setDbPath NOTIFY dbPathChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    enum Roles { IdRole = Qt::UserRole + 1, ProgressStateRole, ProgressFractionRole, SubtitleRole, TitleRole, YearRole, PosterRole, DurationRole, GenresRole, PathRole, SynopsisRole };

    explicit MovieModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString dbPath() const { return m_dbPath; }
    void setDbPath(const QString &p);
    QString filter() const { return m_filter; }
    void setFilter(const QString &f);
    int total() const { return m_all.size(); }
    QString error() const { return m_error; }

    /** Vuelve a leer la base (tras un escaneo o una importación de metadata hechos por el backend). */
    Q_INVOKABLE void reload() { load(); applyFilterAfterReload(); }
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QVariantMap getById(const QString &id) const;
    /** Guarda el progreso como lo hace Electron (upsert en watch_progresses, usuario NULL). Solo en copias de .dev-data. */
    Q_INVOKABLE bool saveProgress(const QString &id, double position, const QString &state,
                                  const QString &audioId = {}, const QString &subId = {});
    bool writable() const;
    /** Filas de Inicio (RF-11): kind = continue | queue | recent | watched. Devuelve tarjetas ligeras. */
    Q_INVOKABLE QVariantList homeRow(const QString &kind, int limit = 20) const;
    Q_INVOKABLE bool fileExists(const QString &path) const;

signals:
    void dbPathChanged();
    void filterChanged();
    void countChanged();
    void errorChanged();
    void homeChanged(); // cambió algo que afecta a las filas de Inicio (progreso)

private:
    struct Movie {
        QString id, title, posterUrl, genres, path, synopsis;
        QString progressAudio, progressSub;
        qint64 createdAt = 0, lastPlayed = 0, queuedAt = 0; // creado_en, última reproducción, agregado a «Próximas a ver»
        QString origTitle, backdropUrl, studio, lang, castJson, crewJson, progressState;
        double progressPos = 0;
        int year = 0, duration = 0;
        QString folded; // título normalizado (sin tildes, minúsculas) para buscar
    };
    void load();
    void applyFilterAfterReload() { if (!m_filter.isEmpty()) applyFilter(); }
    QVariantMap toMap(const Movie &m) const;
    QVariantMap card(const Movie &m) const;
    void refreshRow(const QString &id);
    void applyFilter();
    static QString fold(const QString &s);

    QString m_dbPath, m_filter, m_error;
    QVector<Movie> m_all, m_view;
};
