#pragma once
#include <QSqlDatabase>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

/**
 * Base de datos de la colección (cinemalove.sqlite) del backend integrado. Aplica las reglas de specs/heredadas/02 §2: bloqueo de un solo
 * escritor (compartido con Electron/Qt por PID), respaldo automático (VACUUM INTO cada 7 días, conserva 5), comprobación de la versión del
 * esquema y migraciones propias (solo tablas cl_*). Cada hilo usa su propia conexión (QSqlDatabase no se comparte entre hilos).
 */
class Store
{
public:
    struct OpenResult {
        bool ok = false;
        QString code;          // bloqueada | version-antigua | error
        QString message;
        QString backup;        // respaldo creado al abrir (nombre) o vacío
        QString warning;       // p. ej. base más nueva: solo lectura
        bool created = false;
        bool readOnly = false;
    };

    static OpenResult open(const QString &folder);
    static void close();                      // suelta los bloqueos propios y cierra
    static QString folder();
    static QString dbFile();
    static bool readOnly();

    /** Conexión del hilo actual (se crea la primera vez que se pide). */
    static QSqlDatabase db();
    static void closeThreadConnection();

    static QVector<QVariantMap> rows(const QString &sql, const QVariantList &binds = {});
    static QVariantMap row(const QString &sql, const QVariantList &binds = {});   // vacío si no hay
    static QVariant scalar(const QString &sql, const QVariantList &binds = {});
    static bool exec(const QString &sql, const QVariantList &binds = {}, QString *error = nullptr);
};
