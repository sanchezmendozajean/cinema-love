#include <clocale>

#include <QCommandLineParser>
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QOpenGLContext>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QTextStream>

namespace {

QFile *g_log = nullptr;

void logHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    static const char *names[] = {"DEBUG", "WARN ", "CRIT ", "FATAL", "INFO "};
    const QString line = QStringLiteral("%1 %2 %3\n")
                             .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), names[type], msg);
    fputs(line.toUtf8().constData(), stderr);
    if (g_log && g_log->isOpen()) {
        g_log->write(line.toUtf8());
        g_log->flush();
    }
}

} // namespace

int main(int argc, char *argv[])
{
    // Qt Quick debe usar OpenGL: la API de render de libmpv dibuja en un FBO de OpenGL (specs/02 §2.1).
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setVersion(3, 3);
    fmt.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(fmt);

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("CinemaLove"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    std::setlocale(LC_NUMERIC, "C"); // requisito de libmpv

    // El archivo de log se deja vivo hasta el final del proceso: Qt sigue emitiendo mensajes al destruir la aplicación.
    g_log = new QFile(QCoreApplication::applicationDirPath() + QStringLiteral("/cinemalove-spike.log"));
    g_log->open(QIODevice::WriteOnly | QIODevice::Truncate);
    qInstallMessageHandler(logHandler);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("CinemaLove (Qt 6 + libmpv) — spike de la Fase 0"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("archivo"), QStringLiteral("Archivo o URL a reproducir"));
    QCommandLineOption selftest(QStringLiteral("selftest"), QStringLiteral("Secuencia automática (seek, pistas, pausa) y salida"));
    QCommandLineOption statsOut(QStringLiteral("stats-out"), QStringLiteral("Ruta del JSON de métricas del selftest"), QStringLiteral("ruta"));
    QCommandLineOption subFile(QStringLiteral("sub"), QStringLiteral("Subtítulo externo a cargar"), QStringLiteral("ruta"));
    parser.addOptions({selftest, statsOut, subFile});
    parser.process(app);

    QQmlApplicationEngine engine;
    QVariantMap args;
    args["file"] = parser.positionalArguments().value(0);
    args["selftest"] = parser.isSet(selftest);
    args["statsOut"] = parser.value(statsOut);
    args["sub"] = parser.value(subFile);
    engine.rootContext()->setContextProperty(QStringLiteral("spikeArgs"), args);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("CinemaLove", "Main");

    if (auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0))) {
        QObject::connect(win, &QQuickWindow::sceneGraphInitialized, win, [win] {
            const auto api = win->rendererInterface()->graphicsApi();
            qInfo("Scene graph listo. API gráfica: %d (OpenGL=%d)", int(api), int(QSGRendererInterface::OpenGL));
            if (auto *ctx = QOpenGLContext::currentContext())
                qInfo("OpenGL: %s %d.%d", ctx->isOpenGLES() ? "ES" : "Desktop", ctx->format().majorVersion(), ctx->format().minorVersion());
        }, Qt::DirectConnection);
        QObject::connect(win, &QQuickWindow::sceneGraphError, win, [](QQuickWindow::SceneGraphError, const QString &m) {
            qCritical("sceneGraphError: %s", qPrintable(m));
        });
    }
    return app.exec();
}
