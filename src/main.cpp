#include <clocale>

#include <QCommandLineParser>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>
#include <QStandardPaths>
#include "BackendLauncher.h"
#include "ClImageProvider.h"
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QQmlNetworkAccessManagerFactory>
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

// Los pósters se descargan de TMDb y se guardan en una caché de disco (solo desarrollo; la caché definitiva compartida
// con images/ llega con el ImageProvider, specs/04 §4).
class CachedNam : public QQmlNetworkAccessManagerFactory
{
public:
    explicit CachedNam(QString dir) : m_dir(std::move(dir)) {}
    QNetworkAccessManager *create(QObject *parent) override
    {
        auto *nam = new QNetworkAccessManager(parent);
        auto *cache = new QNetworkDiskCache(nam);
        cache->setCacheDirectory(m_dir);
        cache->setMaximumCacheSize(512LL * 1024 * 1024);
        nam->setCache(cache);
        return nam;
    }
private:
    QString m_dir;
};

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
    QCommandLineOption dataDb(QStringLiteral("database"), QStringLiteral("Copia de cinemalove.sqlite (por defecto .dev-data/)"), QStringLiteral("ruta"));
    QCommandLineOption catShot(QStringLiteral("catalog-shot"), QStringLiteral("Captura el catálogo a un PNG (con --search opcional) y sale"), QStringLiteral("ruta"));
    QCommandLineOption searchOpt(QStringLiteral("search"), QStringLiteral("Texto de búsqueda inicial"), QStringLiteral("texto"));
    QCommandLineOption openFirst(QStringLiteral("open-first"), QStringLiteral("Abre la primera película del catálogo filtrado (prueba)"));
    QCommandLineOption playOpt(QStringLiteral("play"), QStringLiteral("Con --open-first: reproduce en vez de mostrar la ficha"));
    QCommandLineOption progTest(QStringLiteral("progress-test"), QStringLiteral("Prueba de guardar/reanudar progreso con la primera coincidencia de --search"));
    QCommandLineOption sectionOpt(QStringLiteral("section"), QStringLiteral("Sección inicial: home | catalog"), QStringLiteral("seccion"));
    QCommandLineOption scrollOpt(QStringLiteral("home-scroll"), QStringLiteral("Desplaza Inicio N píxeles antes de la captura (prueba)"), QStringLiteral("px"));
    QCommandLineOption adminScan(QStringLiteral("admin-scan"), QStringLiteral("Prueba: inicia «Escanear todo» en Administración y muestra Tareas"));
    QCommandLineOption noBackend(QStringLiteral("no-backend"), QStringLiteral("No inicia el backend (las imágenes se descargan directamente de TMDb)"));
    QCommandLineOption uiTest(QStringLiteral("ui-test"), QStringLiteral("Acción de prueba tras cargar: collection | menu | markconfirm"), QStringLiteral("nombre"));
    QCommandLineOption adminTab(QStringLiteral("admin-tab"), QStringLiteral("Pestaña inicial de Administración (prueba)"), QStringLiteral("pestaña"));
    QCommandLineOption multi(QStringLiteral("multi"), QStringLiteral("Permite varias instancias (por defecto solo una)"));
    parser.addOptions({multi, adminTab, uiTest, noBackend, adminScan, scrollOpt, sectionOpt, selftest, statsOut, subFile, dataDb, catShot, searchOpt, openFirst, playOpt, progTest});
    parser.process(app);

    // Instancia única (specs/01 §5): una segunda ejecución avisa a la primera, que se trae al frente, y termina.
    QLocalServer instanceServer;
    if (!parser.isSet(multi) && !parser.isSet(selftest) && !parser.isSet(catShot)) {
        const QString name = QStringLiteral("cinemalove-qt6-instancia");
        QLocalSocket probe;
        probe.connectToServer(name);
        if (probe.waitForConnected(300)) {
            probe.write("raise\n");
            probe.waitForBytesWritten(500);
            return 0;
        }
        QLocalServer::removeServer(name); // resto de un cierre brusco
        instanceServer.listen(name);
    }
    BackendLauncher backend; // declarado antes que el motor: debe sobrevivirle (los bindings QML lo leen al destruirse)
    QQmlApplicationEngine engine;
    const QString devData = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../../.dev-data"));
    // El backend (sidecar Node) se inicia desde el arranque: las imágenes y la administración dependen de él.
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.addImageProvider(QStringLiteral("cl"), new ClImageProvider(&backend));
    engine.setNetworkAccessManagerFactory(new CachedNam(QDir::cleanPath(devData + QStringLiteral("/img-cache"))));
    QVariantMap args;
    args["file"] = parser.positionalArguments().value(0);
    args["selftest"] = parser.isSet(selftest);
    args["statsOut"] = parser.value(statsOut);
    args["catalogShot"] = parser.value(catShot);
    args["openFirst"] = parser.isSet(openFirst);
    args["play"] = parser.isSet(playOpt);
    args["progressTest"] = parser.isSet(progTest);
    args["section"] = parser.value(sectionOpt);
    args["homeScroll"] = parser.value(scrollOpt).toInt();
    args["adminScan"] = parser.isSet(adminScan);
    args["uiTest"] = parser.value(uiTest);
    args["adminTab"] = parser.value(adminTab);
    args["search"] = parser.value(searchOpt);
    args["sub"] = parser.value(subFile);
    args["db"] = parser.isSet(dataDb) ? parser.value(dataDb) : QDir::cleanPath(devData + QStringLiteral("/cinemalove.sqlite"));
    {   // Carpeta de datos del backend: solo si el archivo se llama cinemalove.sqlite (el backend siempre abre ese nombre).
        const QFileInfo dbInfo(args["db"].toString());
        args["dataDir"] = dbInfo.fileName() == QLatin1String("cinemalove.sqlite") ? dbInfo.absolutePath() : QString();
    }
    engine.rootContext()->setContextProperty(QStringLiteral("spikeArgs"), args);

    {
        const QString dataDir = args["dataDir"].toString();
        if (!parser.isSet(noBackend) && !dataDir.isEmpty()) backend.start(dataDir);
    }
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
    // Ventana: posición/tamaño persistentes (se descarta si quedaría fuera de toda pantalla) y «traer al frente» desde otra instancia.
    if (auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0))) {
        const bool testing = parser.isSet(selftest) || parser.isSet(catShot);
        QSettings st(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/window.ini"), QSettings::IniFormat);
        static QRect lastNormal;
        if (!testing) {
            const QRect r = st.value(QStringLiteral("geometry")).toRect();
            if (r.isValid() && r.width() >= win->minimumWidth() && r.height() >= win->minimumHeight() && QGuiApplication::screenAt(r.center()))
                win->setGeometry(r);
            if (st.value(QStringLiteral("maximized")).toBool()) win->showMaximized();
        }
        lastNormal = win->geometry();
        auto remember = [win]() { if (win->visibility() == QWindow::Windowed) lastNormal = win->geometry(); };
        for (auto sig : {&QWindow::xChanged, &QWindow::yChanged, &QWindow::widthChanged, &QWindow::heightChanged})
            QObject::connect(win, sig, win, remember);
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [win, testing]() {
            if (testing) return;
            QSettings out(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/window.ini"), QSettings::IniFormat);
            out.setValue(QStringLiteral("geometry"), lastNormal);
            out.setValue(QStringLiteral("maximized"), win->visibility() == QWindow::Maximized);
            out.sync();
        });
        QObject::connect(&instanceServer, &QLocalServer::newConnection, win, [win, &instanceServer]() {
            while (QLocalSocket *c = instanceServer.nextPendingConnection()) { c->deleteLater(); }
            if (win->visibility() == QWindow::Minimized) win->showNormal();
            win->raise();
            win->requestActivate();
        });
    }
    return app.exec();
}
