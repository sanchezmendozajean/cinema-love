#include "BackendLauncher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QtConcurrent>
#include <QFutureWatcher>

#include "../server/ApiServer.h"
#include "../server/Store.h"
#include "../client/ClientBackend.h"

#include "../server/ApiServer.h"
#include "../server/Store.h"

BackendLauncher::BackendLauncher(QObject *parent) : QObject(parent)
{
    // Respaldo: si la app termina sin pasar por QML, el backend no se queda huérfano.
    connect(qApp, &QCoreApplication::aboutToQuit, this, &BackendLauncher::stop);
}

BackendLauncher::~BackendLauncher() { stop(); }

QString BackendLauncher::findScript()
{
    const QDir app(QCoreApplication::applicationDirPath());
    for (const QString &rel : {QStringLiteral("backend/main.cjs"), QStringLiteral("../../../backend/dist/main.cjs")}) {
        const QString p = QDir::cleanPath(app.absoluteFilePath(rel));
        if (QFileInfo::exists(p)) return p;
    }
    return {};
}

QString BackendLauncher::findNode()
{
    // Solo el Node incluido con la aplicación (autocontenida: nunca el del sistema ni el PATH).
    const QDir app(QCoreApplication::applicationDirPath());
    for (const QString &rel : {QStringLiteral("backend/node.exe"), QStringLiteral("backend/node"),                     // instalada
                               QStringLiteral("../../../third_party/node/node.exe"), QStringLiteral("../../../third_party/node/node")}) { // desarrollo
        const QString p = QDir::cleanPath(app.absoluteFilePath(rel));
        if (QFileInfo::exists(p)) return p;
    }
    return {};
}

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
    if (m_proc) return;
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
    m_restarts = 0;
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
    if (m_proc) return;
    m_errorCode.clear();
    m_info.clear();
    m_failed = false;
    m_clientMode = true;
    m_dataDir = QDir(clientDir).absolutePath();
    QDir().mkpath(m_dataDir);
    m_restarts = 0;
    m_stopping = false;
    // Modo Cliente integrado en la app (sin Node): conexión, proxy local y espejo viven en este proceso.
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

void BackendLauncher::launch()
{
    const QString script = findScript(), node = findNode();
    if (script.isEmpty() || node.isEmpty()) {
        setState(QStringLiteral("error"), script.isEmpty() ? tr("No se encuentra backend/dist/main.cjs (ejecuta scripts/build-backend.ps1).")
                                                           : tr("No se encuentra el Node incluido (third_party/node/node.exe)."));
        return;
    }
    setState(QStringLiteral("iniciando"));
    m_buffer.clear();
    m_proc = new QProcess(this);
    // El backend toma la configuración y la raíz de la app de variables de entorno (ver backend/src/electron-shim.ts).
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QDir dir(m_dataDir);
    if (m_clientMode) env.insert(QStringLiteral("CL_CLIENTE_DIR"), m_dataDir);
    else env.insert(QStringLiteral("CL_DATA_DIR"), m_dataDir);
    env.insert(QStringLiteral("CL_APP_ROOT"), m_clientMode ? m_dataDir : dir.dirName() == QLatin1String(".dev-data") ? QDir::cleanPath(dir.absoluteFilePath(QStringLiteral(".."))) : m_dataDir);
    env.insert(QStringLiteral("CL_SIN_FFMPEG"), QStringLiteral("1")); // la UI Qt reproduce con mpv: sin sesiones de transcodificación
    m_proc->setProcessEnvironment(env);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &BackendLauncher::onStdout);
    connect(m_proc, &QProcess::finished, this, &BackendLauncher::onFinished);
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) setState(QStringLiteral("error"), tr("No se pudo ejecutar Node.js."));
    });
    m_proc->start(node, {script});
}

void BackendLauncher::onStdout()
{
    m_buffer += QString::fromUtf8(m_proc->readAllStandardOutput());
    int nl;
    while ((nl = m_buffer.indexOf(QLatin1Char('\n'))) >= 0) {
        const QString line = m_buffer.left(nl).trimmed();
        m_buffer.remove(0, nl + 1);
        if (line.isEmpty()) continue;
        if (line.startsWith(QLatin1String("CL_READY "))) {
            const QJsonObject o = QJsonDocument::fromJson(line.mid(9).toUtf8()).object();
            m_info = o.value(QStringLiteral("datos")).toObject().toVariantMap();
            m_baseUrl = o.value(QStringLiteral("baseUrl")).toString();
            m_token = o.value(QStringLiteral("token")).toString();
            appendLog(QStringLiteral("Backend listo en ") + m_baseUrl);
            emit readyChanged();
            setState(QStringLiteral("listo"));
        } else if (line.startsWith(QLatin1String("CL_ERROR "))) {
            const QJsonObject o = QJsonDocument::fromJson(line.mid(9).toUtf8()).object();
            const QJsonObject datos = o.value(QStringLiteral("datos")).toObject();
            m_failed = true;
            m_errorCode = datos.value(QStringLiteral("motivo")).toString(QStringLiteral("error"));
            const QString msg = datos.value(QStringLiteral("mensaje")).toString(o.value(QStringLiteral("mensaje")).toString(line.mid(9)));
            setState(QStringLiteral("error"), msg);
            appendLog(line);
        } else {
            appendLog(line);
        }
    }
}

void BackendLauncher::onFinished(int code, QProcess::ExitStatus)
{
    QProcess *p = m_proc;
    m_proc = nullptr;
    if (p) p->deleteLater();
    m_baseUrl.clear();
    m_token.clear();
    m_apiBase.clear();
    m_apiToken.clear();
    emit readyChanged();
    if (m_failed) return; // conserva el estado de error con su mensaje
    if (m_stopping) {
        setState(QStringLiteral("detenido"));
        return;
    }
    appendLog(tr("El backend terminó inesperadamente (código %1).").arg(code));
    if (m_restarts++ < 1) { // un reintento automático; los datos no se pierden (SQLite transaccional)
        launch();
        return;
    }
    setState(QStringLiteral("error"), tr("El backend se detuvo y no pudo reiniciarse."));
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
    if (!m_proc) return;
    m_stopping = true;
    QProcess *p = m_proc;
    p->write("quit\n");
    p->closeWriteChannel();
    if (!p->waitForFinished(4000)) {
        p->kill();
        p->waitForFinished(2000);
    }
}
