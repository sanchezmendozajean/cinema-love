#pragma once
#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>

/**
 * Carpeta de datos del usuario (cinemalove.sqlite, images/, config.yaml de datos). Se recuerda en `config.yaml` con la clave
 * `database_directory` (mismo formato que las otras versiones) junto al ejecutable (portátil) o, si no es escribible, en la
 * carpeta de configuración del usuario. Solo inspecciona y apunta: abrir la base (respaldo, bloqueo, migraciones) lo hace el backend.
 */
class DataFolder : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    Q_PROPERTY(QString dbFile READ dbFile NOTIFY pathChanged)
    Q_PROPERTY(bool fromOverride READ fromOverride CONSTANT)

public:
    explicit DataFolder(QObject *parent = nullptr);

    QString path() const { return m_path; }
    QString dbFile() const;
    bool fromOverride() const { return m_override; }

    /** Desarrollo/pruebas: usa esta carpeta sin tocar config.yaml. */
    void setOverride(const QString &dir, const QString &fileName = QStringLiteral("cinemalove.sqlite"));

    /** Qué hay en `dir`: {exists, writable, hasCollection, locked, bibliotecas, peliculas, series, sizeMb, error}. Solo lee. */
    Q_INVOKABLE QVariantMap inspect(const QString &dir) const;
    Q_INVOKABLE QString suggested() const;
    /** Recuerda `dir` en config.yaml. Devuelve false (y `lastError`) si no se pudo escribir. */
    Q_INVOKABLE bool apply(const QString &dir);
    /** Olvida la carpeta (borra solo el config.yaml puntero; no toca ningún dato) para volver al asistente. */
    Q_INVOKABLE void forget();
    Q_INVOKABLE QStringList backups() const;
    Q_INVOKABLE QString configFile() const;
    Q_INVOKABLE QString lastError() const { return m_error; }
    Q_INVOKABLE void openInExplorer() const;

signals:
    void pathChanged();

private:
    QString configDir() const;
    QString readConfigured() const;

    QString m_path, m_error;
    bool m_override = false;
    QString m_fileName = QStringLiteral("cinemalove.sqlite");
};
