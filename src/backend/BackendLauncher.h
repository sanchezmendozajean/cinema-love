#pragma once
#include <QObject>
#include <QProcess>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantMap>

/**
 * Ciclo de vida del sidecar Node (specs/03 §2): lo lanza con la carpeta de datos, espera `CL_READY` en su salida, vigila que siga
 * vivo (un reintento automático) y lo cierra con la app. El sidecar también se cierra solo si se cierra su stdin (la app murió).
 */
class BackendLauncher : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)       // detenido | iniciando | listo | error
    Q_PROPERTY(QString baseUrl READ baseUrl NOTIFY readyChanged)   // API que usa la interfaz (en Modo Cliente, el proxy al servidor remoto)
    Q_PROPERTY(QString token READ token NOTIFY readyChanged)
    Q_PROPERTY(QString controlUrl READ controlUrl NOTIFY readyChanged)   // solo Modo Cliente: servidor de control del sidecar
    Q_PROPERTY(QString controlToken READ controlToken NOTIFY readyChanged)
    Q_PROPERTY(bool clientMode READ clientMode NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY stateChanged)   // bloqueada | version-antigua | error | ''
    Q_PROPERTY(QVariantMap info READ info NOTIFY readyChanged)           // datos que informa el backend al abrir (respaldo, conteos…)
    Q_PROPERTY(bool requireDevCopy READ requireDevCopy WRITE setRequireDevCopy NOTIFY stateChanged)
    Q_PROPERTY(QString logTail READ logTail NOTIFY logChanged)

public:
    explicit BackendLauncher(QObject *parent = nullptr);
    ~BackendLauncher() override;

    QString state() const { return m_state; }
    QString baseUrl() const { return m_apiBase.isEmpty() ? m_baseUrl : m_apiBase; }
    QString token() const { return m_apiBase.isEmpty() ? m_token : m_apiToken; }
    QString controlUrl() const { return m_baseUrl; }
    QString controlToken() const { return m_token; }
    bool clientMode() const { return m_clientMode; }
    QString error() const { return m_error; }
    QString errorCode() const { return m_errorCode; }
    QVariantMap info() const { return m_info; }
    bool requireDevCopy() const { return m_requireDev; }
    void setRequireDevCopy(bool on) { m_requireDev = on; }
    QString logTail() const { return m_log.join(QLatin1Char('\n')); }

    /** Inicia el backend sobre `dataDir` (carpeta con cinemalove.sqlite). Solo se acepta una copia de `.dev-data/`. */
    Q_INVOKABLE void start(const QString &dataDir);
    /** Detiene el backend (si corre) y lo inicia sobre otra carpeta. */
    Q_INVOKABLE void restart(const QString &dataDir);
    /** Modo Cliente: inicia el sidecar sin colección local; `clientDir` guarda cliente_datos.sqlite y el espejo del catálogo. */
    Q_INVOKABLE void startClient(const QString &clientDir);
    Q_INVOKABLE void restartClient(const QString &clientDir);
    /** Con una conexión activa, la interfaz habla con el proxy local del servidor remoto en vez de con el sidecar. */
    Q_INVOKABLE void setApi(const QString &baseUrl, const QString &token);
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
    QString m_errorCode;
    QVariantMap m_info;
    bool m_requireDev = false;
    QString m_dataDir, m_state = QStringLiteral("detenido"), m_baseUrl, m_token, m_error, m_buffer;
    QStringList m_log;
    bool m_clientMode = false;
    QString m_apiBase, m_apiToken;
    bool m_stopping = false;
    bool m_failed = false;   // el backend informó un error de apertura (CL_ERROR): no se reintenta
    int m_restarts = 0;
};
