#include <clocale>

#include <QCommandLineParser>
#include <QDir>
#include <QScreen>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>
#include <QStandardPaths>
#include "AppSettings.h"
#include "server/ApiServer.h"
#include "server/Store.h"
#include "client/ClientBackend.h"
#include "BackendLauncher.h"
#include "DataFolder.h"
#include "TestInput.h"
#include "ProgressWriter.h"
#include "ClImageProvider.h"
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QQmlNetworkAccessManagerFactory>
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QDir>
#include <QFontDatabase>
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
    // Tipografías opcionales (OFL) junto al ejecutable o en third_party/fonts: Fraunces para títulos, Inter para la interfaz.
    for (const QString &dir : {QCoreApplication::applicationDirPath() + QStringLiteral("/fonts"),
                               QCoreApplication::applicationDirPath() + QStringLiteral("/../../third_party/fonts"),
                               QCoreApplication::applicationDirPath() + QStringLiteral("/../../../third_party/fonts")}) {
        const auto files = QDir(dir).entryList({QStringLiteral("*.ttf"), QStringLiteral("*.otf")}, QDir::Files);
        for (const QString &f : files)
            QFontDatabase::addApplicationFont(dir + QLatin1Char('/') + f);
    }
    if (QFontDatabase::families().contains(QStringLiteral("Inter"))) {
        QFont base = app.font();
        base.setFamily(QStringLiteral("Inter"));
        app.setFont(base);
    }
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
    QCommandLineOption nativeScroll(QStringLiteral("native-scroll"), QStringLiteral("Prueba: usa el desplazamiento nativo de Qt (para medir)"));
    QCommandLineOption notchOpt(QStringLiteral("notch-pixels"), QStringLiteral("Prueba: píxeles por muesca de la rueda"), QStringLiteral("px"));
    QCommandLineOption clientDirOpt(QStringLiteral("client-dir"), QStringLiteral("Prueba del Modo Cliente: carpeta de cliente_datos.sqlite y el espejo (dentro de .dev-data/)"), QStringLiteral("ruta"));
    QCommandLineOption clientUrlOpt(QStringLiteral("client-url"), QStringLiteral("Prueba del Modo Cliente: se conecta a esta URL"), QStringLiteral("url"));
    QCommandLineOption clientKeyOpt(QStringLiteral("client-key"), QStringLiteral("Prueba del Modo Cliente: token (API key) del servidor"), QStringLiteral("token"));
    QCommandLineOption serveOpt(QStringLiteral("serve"), QStringLiteral("Prueba del backend integrado: solo API HTTP sobre esta carpeta de datos (dentro de .dev-data/)"), QStringLiteral("carpeta"));
    QCommandLineOption servePortOpt(QStringLiteral("serve-port"), QStringLiteral("Puerto del modo --serve (0 = libre)"), QStringLiteral("puerto"), QStringLiteral("0"));
    QCommandLineOption clientServeOpt(QStringLiteral("client-serve"), QStringLiteral("Prueba del Modo Cliente integrado: solo el servicio de control, sobre esta carpeta (dentro de .dev-data/)"), QStringLiteral("carpeta"));
    parser.addOptions({clientServeOpt, serveOpt, servePortOpt, clientDirOpt, clientUrlOpt, clientKeyOpt, nativeScroll, notchOpt, multi, adminTab, uiTest, noBackend, adminScan, scrollOpt, sectionOpt, selftest, statsOut, subFile, dataDb, catShot, searchOpt, openFirst, playOpt, progTest});
    parser.process(app);

    // Instancia única (specs/01 §5): una segunda ejecución avisa a la primera, que se trae al frente, y termina.
    QLocalServer instanceServer;
    if (parser.isSet(clientServeOpt)) {
        const QString dir = QDir(parser.value(clientServeOpt)).absolutePath();
        if (!QDir::fromNativeSeparators(dir).contains(QStringLiteral("/.dev-data/"))) {
            fprintf(stderr, "--client-serve solo acepta carpetas dentro de .dev-data/\n");
            return 3;
        }
        ClientBackend cb;
        const QString err = cb.start(dir);
        if (!err.isEmpty()) { printf("CL_ERROR {\"mensaje\":\"%s\"}\n", qPrintable(err)); fflush(stdout); return 2; }
        printf("CL_READY {\"baseUrl\":\"%s\",\"token\":\"%s\",\"modo\":\"cliente\"}\n", qPrintable(cb.controlUrl()), qPrintable(cb.controlToken()));
        fflush(stdout);
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&cb] { cb.stop(); });
        return app.exec();
    }
    // Backend integrado en modo de pruebas: solo la API HTTP (sin interfaz), para compararla con la del backend Node.
    if (parser.isSet(serveOpt)) {
        const QString dir = QDir(parser.value(serveOpt)).absolutePath();
        if (!QDir::fromNativeSeparators(dir).contains(QStringLiteral("/.dev-data/"))) {
            fprintf(stderr, "--serve solo acepta carpetas dentro de .dev-data/\n");
            return 3;
        }
        const Store::OpenResult r = Store::open(dir);
        if (!r.ok) {
            printf("CL_ERROR {\"motivo\":\"%s\",\"mensaje\":\"%s\"}\n", qPrintable(r.code), qPrintable(r.message));
            fflush(stdout);
            return 2;
        }
        ApiServer api;
        const quint16 port = api.start(parser.value(servePortOpt).toUShort());
        if (!port) { printf("CL_ERROR {\"motivo\":\"error\",\"mensaje\":\"no se pudo escuchar\"}\n"); fflush(stdout); return 2; }
        printf("CL_READY {\"baseUrl\":\"%s\",\"token\":\"%s\",\"respaldo\":\"%s\"}\n", qPrintable(api.baseUrl()), qPrintable(api.adminToken()), qPrintable(r.backup));
        fflush(stdout);
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&api] { api.stop(); Store::close(); });
        return app.exec();
    }
    // Pruebas automatizadas: solo sobre copias de .dev-data/ y sin instancia única ni persistencia de ventana.
    const bool testMode = parser.isSet(selftest) || parser.isSet(catShot) || parser.isSet(uiTest) || parser.isSet(progTest) || parser.isSet(adminScan);
    if (!parser.isSet(multi) && !testMode) {
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
    ProgressWriter::setRequireDevCopy(testMode);
    // Carpeta de datos: la de config.yaml (asistente de bienvenida) o, en desarrollo, la de --database.
    DataFolder dataFolder;
    {
        QString override = parser.isSet(dataDb) ? parser.value(dataDb) : QString();
        if (override.isEmpty() && testMode)
            override = QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../../.dev-data/cinemalove.sqlite")));
        if (!override.isEmpty()) {
            const QFileInfo fi(override);
            dataFolder.setOverride(fi.absolutePath(), fi.fileName());
        }
        if (testMode && !dataFolder.dbFile().isEmpty() && !ProgressWriter::isDevCopy(dataFolder.dbFile())) {
            fprintf(stderr, "Modo de pruebas: la base debe estar en una carpeta .dev-data/ (nunca datos reales): %s\n", qPrintable(dataFolder.dbFile()));
            return 3;
        }
    }
    BackendLauncher backend; // declarado antes que el motor: debe sobrevivirle (los bindings QML lo leen al destruirse)
    backend.setRequireDevCopy(testMode);
    QQmlApplicationEngine engine;
    // El backend (sidecar Node) se inicia desde el arranque: las imágenes y la administración dependen de él.
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("dataFolder"), &dataFolder);
    TestInput testInput;
    engine.rootContext()->setContextProperty(QStringLiteral("testInput"), &testInput);
    AppSettings prefs;
    engine.rootContext()->setContextProperty(QStringLiteral("prefs"), &prefs);
    engine.addImageProvider(QStringLiteral("cl"), new ClImageProvider(&backend));
    engine.setNetworkAccessManagerFactory(new CachedNam(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/img")));
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
    args["db"] = dataFolder.dbFile();
    // Carpeta de datos del backend: solo si el archivo se llama cinemalove.sqlite (el backend siempre abre ese nombre).
    args["dataDir"] = QFileInfo(dataFolder.dbFile()).fileName() == QLatin1String("cinemalove.sqlite") ? dataFolder.path() : QString();
    args["testMode"] = testMode;
    args["nativeScroll"] = parser.isSet(nativeScroll);
    args["notchPixels"] = parser.value(notchOpt).toDouble();
    args["noBackend"] = parser.isSet(noBackend);
    // Modo Cliente: recordado (se reconecta al abrir) o de prueba (--client-dir/--client-url, solo dentro de .dev-data/).
    QString clientDir = parser.value(clientDirOpt);
    if (!clientDir.isEmpty() && !QDir::fromNativeSeparators(QDir(clientDir).absolutePath()).contains(QStringLiteral("/.dev-data/"))) {
        fprintf(stderr, "--client-dir debe estar dentro de .dev-data/\n");
        return 3;
    }
    const bool clientAuto = !testMode && prefs.value(QStringLiteral("modo")).toString() == QLatin1String("cliente");
    if (clientDir.isEmpty()) clientDir = AppSettings::clientDir();
    args["clientDir"] = clientDir;
    args["clientAuto"] = clientAuto || !parser.value(clientDirOpt).isEmpty();
    args["clientConnect"] = parser.value(clientUrlOpt);
    args["clientKey"] = parser.value(clientKeyOpt);
    engine.rootContext()->setContextProperty(QStringLiteral("spikeArgs"), args);

    {
        const QString dataDir = args["dataDir"].toString();
        if (!parser.isSet(noBackend)) {
            if (args["clientAuto"].toBool()) backend.startClient(args["clientDir"].toString());
            else if (!dataDir.isEmpty()) backend.start(dataDir);
        }
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
        const bool testing = testMode;
        QSettings st(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/window.ini"), QSettings::IniFormat);
        static QRect lastNormal;
        if (!testing) {
            const QRect r = st.value(QStringLiteral("geometry")).toRect();
            // Una geometría del tamaño de la pantalla no es una ventana «normal»: Qt la trataría como pantalla completa (sin barra de título).
            const QScreen *sc = QGuiApplication::screenAt(r.center());
            const bool coversScreen = sc && r.width() >= sc->availableGeometry().width() && r.height() >= sc->availableGeometry().height();
            if (r.isValid() && r.width() >= win->minimumWidth() && r.height() >= win->minimumHeight() && sc && !coversScreen)
                win->setGeometry(r);
            if (st.value(QStringLiteral("maximized")).toBool()) win->showMaximized();
        }
        lastNormal = win->visibility() == QWindow::Windowed ? win->geometry() : QRect();
        if (!lastNormal.isValid()) {   // arrancó maximizada: al restaurar, un tamaño razonable en vez de la pantalla entera
            const QRect av = win->screen() ? win->screen()->availableGeometry() : QRect(0, 0, 1920, 1080);
            const QSize sz(qMin(1280, av.width() * 4 / 5), qMin(720, av.height() * 4 / 5));
            lastNormal = QRect(av.center() - QPoint(sz.width() / 2, sz.height() / 2), sz);
        }
        // Solo se recuerda una ventana en estado normal y más pequeña que la pantalla: al maximizar, la geometría cambia antes que el estado
        // y se guardaba el tamaño de la pantalla, de modo que «restaurar» devolvía una ventana que Qt consideraba pantalla completa.
        auto remember = [win]() {
            if (win->visibility() != QWindow::Windowed) return;
            const QScreen *sc = win->screen();
            const QRect g = win->geometry();
            if (sc && g.width() >= sc->availableGeometry().width() && g.height() >= sc->availableGeometry().height()) return;
            lastNormal = g;
        };
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
