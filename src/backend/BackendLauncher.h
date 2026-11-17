#pragma once
#include <QObject>
#include <QProcess>
#include <QQmlEngine>
#include <QStringList>

/**
 * Ciclo de vida del sidecar Node (specs/03 §2): lo lanza con la carpeta de datos, espera `CL_READY` en su salida, vigila que siga
 * vivo (un reintento automático) y lo cierra con la app. El sidecar también se cierra solo si se cierra su stdin (la app murió).
 */
class BackendLauncher : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)       // detenido | iniciando | listo | error
    Q_PROPERTY(QString baseUrl READ baseUrl NOTIFY readyChanged)
    Q_PROPERTY(QString token READ token NOTIFY readyChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString logTail READ logTail NOTIFY logChanged)

public:
    explicit BackendLauncher(QObject *parent = nullptr);
    ~BackendLauncher() override;

    QString state() const { return m_state; }
    QString baseUrl() const { return m_baseUrl; }
    QString token() const { return m_token; }
    QString error() const { return m_error; }
    QString logTail() const { return m_log.join(QLatin1Char('\n')); }

    /** Inicia el backend sobre `dataDir` (carpeta con cinemalove.sqlite). Solo se acepta una copia de `.dev-data/`. */
    Q_INVOKABLE void start(const QString &dataDir);
    Q_INVOKABLE void stop();

signals:
    void stateChanged();
    void readyChanged();
    void logChanged();

private:
    void launch();
    void onStdout();
    void onFinished(int code, QProcess::ExitStatus status);
    void setState(const QString &s, const QString &err = {});
    void appendLog(const QString &line);
    static QString findScript();
    static QString findNode();

    QProcess *m_proc = nullptr;
    QString m_dataDir, m_state = QStringLiteral("detenido"), m_baseUrl, m_token, m_error, m_buffer;
    QStringList m_log;
    bool m_stopping = false;
    int m_restarts = 0;
};
