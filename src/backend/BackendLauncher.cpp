#include "BackendLauncher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrent>
#include <QFutureWatcher>

#include "../client/ClientBackend.h"
#include "../server/ApiServer.h"
#include "../server/Store.h"

BackendLauncher::BackendLauncher(QObject *parent) : QObject(parent)
{
    // Respaldo: si la app termina sin pasar por QML, el backend no se queda huérfano.
    connect(qApp, &QCoreApplication::aboutToQuit, this, &BackendLauncher::stop);
}

BackendLauncher::~BackendLauncher() { stop(); }

void BackendLauncher::setState(const QString &s, const QString &err)
{
    m_state = s;
    m_error = err;
    emit stateChanged();
}

void BackendLauncher::appendLog(const QString &line)
{
    m_log << line;
    while (m_log.size() > 60) m_log.removeFirst();
    emit logChanged();
}

void BackendLauncher::start(const QString &dataDir)
{
    // En modo de pruebas automatizadas solo se acepta una copia de desarrollo (nunca datos reales).
    const QString norm = QDir::fromNativeSeparators(QDir(dataDir).absolutePath()) + QLatin1Char('/');
    if (m_requireDev && !norm.contains(QStringLiteral("/.dev-data/"))) {
        m_errorCode = QStringLiteral("error");
        setState(QStringLiteral("error"), tr("En modo de pruebas el backend solo se inicia sobre copias de .dev-data/: %1").arg(dataDir));
        return;
    }
    m_errorCode.clear();
    m_info.clear();
    m_failed = false;
    m_clientMode = false;
    m_dataDir = QDir(dataDir).absolutePath();
    m_stopping = false;
    startLocal(m_dataDir);
}

void BackendLauncher::startLocal(const QString &dataDir)
{
    if (m_localOpening || m_localRunning) return;
    m_localOpening = true;
    setState(QStringLiteral("iniciando"));
    // La apertura (bloqueo, respaldo con VACUUM INTO, migraciones) puede tardar: se hace fuera del hilo de la interfaz.
    auto *watcher = new QFutureWatcher<QPair<Store::OpenResult, QVariantMap>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        const auto res = watcher->result();
        watcher->deleteLater();
        finishLocalOpen(res.first.ok, res.first.code, res.first.message, res.second);
    });
    watcher->setFuture(QtConcurrent::run([dataDir] {
        QVariantMap info;
        const Store::OpenResult r = Store::open(dataDir);
        if (r.ok) {
            info = {{"ok", true}, {"soloLectura", r.readOnly}, {"aviso", r.warning.isEmpty() ? QVariant() : QVariant(r.warning)},
                    {"respaldo", r.backup.isEmpty() ? QVariant() : QVariant(r.backup)},
                    {"bibliotecas", Store::scalar(QStringLiteral("SELECT COUNT(*) FROM libraries")).toInt()},
                    {"peliculas", Store::scalar(QStringLiteral("SELECT COUNT(*) FROM media_items WHERE series_id IS NULL")).toInt()},
                    {"series", Store::scalar(QStringLiteral("SELECT COUNT(*) FROM series")).toInt()},
                    {"episodios", Store::scalar(QStringLiteral("SELECT COUNT(*) FROM media_items WHERE series_id IS NOT NULL")).toInt()}};
            Store::closeThreadConnection();
        }
        return qMakePair(r, info);
    }));
}

void BackendLauncher::finishLocalOpen(bool ok, const QString &code, const QString &message, const QVariantMap &info)
{
    m_localOpening = false;
    if (m_stopping) { if (ok) Store::close(); setState(QStringLiteral("detenido")); return; }
    if (!ok) {
        m_failed = true;
        m_errorCode = code.isEmpty() ? QStringLiteral("error") : code;
        setState(QStringLiteral("error"), message);
        appendLog(message);
        return;
    }
    if (QDir::cleanPath(Store::folder()) != QDir::cleanPath(m_dataDir)) {   // mientras se abría se pidió otra carpeta
        Store::close();
        startLocal(m_dataDir);
        return;
    }
    m_api = std::make_unique<ApiServer>();
    const quint16 port = m_api->start(0);
    if (!port) {
        Store::close();
        m_api.reset();
        m_errorCode = QStringLiteral("error");
        m_failed = true;
        setState(QStringLiteral("error"), tr("No se pudo iniciar el servicio interno."));
        return;
    }
    m_info = info;
    m_baseUrl = m_api->baseUrl();
    m_token = m_api->adminToken();
    m_localRunning = true;
    appendLog(QStringLiteral("Backend integrado listo en ") + m_baseUrl);
    emit readyChanged();
    setState(QStringLiteral("listo"));
}

void BackendLauncher::stopLocal()
{
    if (m_api) m_api.reset();
    if (m_localRunning) Store::close();
    m_localRunning = false;
    m_baseUrl.clear();
    m_token.clear();
}

void BackendLauncher::startClient(const QString &clientDir)
{
    m_errorCode.clear();
    m_info.clear();
    m_failed = false;
    m_clientMode = true;
    m_dataDir = QDir(clientDir).absolutePath();
    QDir().mkpath(m_dataDir);
    m_stopping = false;
    // Modo Cliente integrado en la app: conexión, proxy local y espejo viven en este proceso.
    setState(QStringLiteral("iniciando"));
    m_client = std::make_unique<ClientBackend>();
    const QString err = m_client->start(m_dataDir);
    if (!err.isEmpty()) {
        m_client.reset();
        m_failed = true;
        m_errorCode = QStringLiteral("error");
        setState(QStringLiteral("error"), err);
        return;
    }
    m_baseUrl = m_client->controlUrl();
    m_token = m_client->controlToken();
    appendLog(QStringLiteral("Modo Cliente listo en ") + m_baseUrl);
    emit readyChanged();
    setState(QStringLiteral("listo"));
}

void BackendLauncher::restartClient(const QString &clientDir)
{
    stop();
    m_state = QStringLiteral("detenido");
    startClient(clientDir);
}

void BackendLauncher::setApi(const QString &baseUrl, const QString &token)
{
    m_apiBase = baseUrl;
    m_apiToken = token;
    emit readyChanged();
}

void BackendLauncher::restart(const QString &dataDir)
{
    stop();
    m_state = QStringLiteral("detenido");
    start(dataDir);
}

void BackendLauncher::stop()
{
    if (m_client) {
        m_stopping = true;
        m_client.reset();
        m_baseUrl.clear();
        m_token.clear();
        m_apiBase.clear();
        m_apiToken.clear();
        setState(QStringLiteral("detenido"));
        emit readyChanged();
        return;
    }
    if (m_localRunning || m_localOpening) {
        m_stopping = true;
        const bool wasOpening = m_localOpening;
        stopLocal();
        if (!wasOpening) { setState(QStringLiteral("detenido")); emit readyChanged(); }
        return;
    }
}
