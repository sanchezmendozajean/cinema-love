#include "BackendLauncher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>

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
    m_dataDir = QDir(dataDir).absolutePath();
    m_restarts = 0;
    m_stopping = false;
    launch();
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
    env.insert(QStringLiteral("CL_DATA_DIR"), m_dataDir);
    env.insert(QStringLiteral("CL_APP_ROOT"), dir.dirName() == QLatin1String(".dev-data") ? QDir::cleanPath(dir.absoluteFilePath(QStringLiteral(".."))) : m_dataDir);
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
