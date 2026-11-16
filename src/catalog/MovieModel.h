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
    enum Roles { IdRole = Qt::UserRole + 1, TitleRole, YearRole, PosterRole, DurationRole, GenresRole, PathRole, SynopsisRole };

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

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE bool fileExists(const QString &path) const;

signals:
    void dbPathChanged();
    void filterChanged();
    void countChanged();
    void errorChanged();

private:
    struct Movie {
        QString id, title, posterUrl, genres, path, synopsis;
        int year = 0, duration = 0;
        QString folded; // título normalizado (sin tildes, minúsculas) para buscar
    };
    void load();
    void applyFilter();
    static QString fold(const QString &s);

    QString m_dbPath, m_filter, m_error;
    QVector<Movie> m_all, m_view;
};
