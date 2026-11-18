#pragma once
#include <QSqlDatabase>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

/**
 * Base `cliente_datos.sqlite` (estado de ESTE equipo como cliente: servidores recordados, progreso/colecciones locales con token). Una conexión por
 * hilo. Reglas de specs/heredadas/02 §2: solo cambios aditivos (`cl_*`); un esquema de otra versión no se toca.
 */
class ClientDb
{
public:
    explicit ClientDb(const QString &path) : m_path(path) {}
    ~ClientDb();

    /** Crea o abre la base; devuelve el error (vacío si todo bien). */
    QString open();
    QString path() const { return m_path; }
    QSqlDatabase db() const;
    void closeThreadConnection() const;

    QVector<QVariantMap> rows(const QString &sql, const QVariantList &binds = {}) const;
    QVariantMap row(const QString &sql, const QVariantList &binds = {}) const;
    QVariant scalar(const QString &sql, const QVariantList &binds = {}) const;
    bool exec(const QString &sql, const QVariantList &binds = {}) const;
    void begin() const;     // anidable
    void commit() const;

private:
    QString m_path;
};
