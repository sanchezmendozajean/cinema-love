#include "MpvItem.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QRunnable>
#include <atomic>
#include <memory>
#include <thread>

#include <mpv/client.h>
#include <mpv/render_gl.h>

namespace {

void *getProcAddress(void *ctx, const char *name)
{
    auto *glctx = static_cast<QOpenGLContext *>(ctx);
    if (!glctx)
        return nullptr;
    return reinterpret_cast<void *>(glctx->getProcAddress(name));
}

void onMpvWakeup(void *ctx)
{
    QMetaObject::invokeMethod(static_cast<MpvItem *>(ctx), "processEvents", Qt::QueuedConnection);
}

void onMpvRedraw(void *ctx)
{
    QMetaObject::invokeMethod(static_cast<MpvItem *>(ctx), "doUpdate", Qt::QueuedConnection);
}

} // namespace

/** Dibuja el fotograma de mpv en el FBO de Qt Quick (hilo de render). */
class MpvRenderer : public QQuickFramebufferObject::Renderer
{
public:
    explicit MpvRenderer(MpvItem *item) : m_item(item) {}

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override
    {
        if (!m_item->m_gl) {
            mpv_opengl_init_params glInit{getProcAddress, QOpenGLContext::currentContext()};
            mpv_render_param params[] = {
                {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
                {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
                {MPV_RENDER_PARAM_INVALID, nullptr}};
            if (mpv_render_context_create(&m_item->m_gl, m_item->m_mpv, params) < 0) {
                qCritical("mpv: no se pudo crear el contexto de render");
                return QQuickFramebufferObject::Renderer::createFramebufferObject(size);
            }
            mpv_render_context_set_update_callback(m_item->m_gl, onMpvRedraw, m_item);
        }
        return QQuickFramebufferObject::Renderer::createFramebufferObject(size);
    }

    void render() override
    {
        if (!m_item->m_gl)
            return;
        QQuickWindow *win = m_item->window();
        if (win)
            win->beginExternalCommands();
        QOpenGLFramebufferObject *fbo = framebufferObject();
        mpv_opengl_fbo mpfbo{static_cast<int>(fbo->handle()), fbo->width(), fbo->height(), 0};
        int flipY = 0;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flipY},
            {MPV_RENDER_PARAM_INVALID, nullptr}};
        mpv_render_context_render(m_item->m_gl, params);
        if (win)
            win->endExternalCommands();
    }

private:
    MpvItem *m_item;
};

MpvItem::MpvItem(QQuickItem *parent) : QQuickFramebufferObject(parent)
{
    m_mpv = mpv_create();
    if (!m_mpv) {
        qCritical("mpv: mpv_create falló");
        return;
    }
    setOption("vo", "libmpv");
    setOption("hwdec", "auto-safe");
    setOption("keep-open", "yes");
    setOption("input-default-bindings", "no");
    setOption("input-vo-keyboard", "no");
    setOption("osc", "no");
    setOption("osd-level", "0");
    setOption("volume-max", "150");
    setOption("sub-auto", "fuzzy");
    setOption("cache", "yes");
    setOption("demuxer-max-bytes", "150MiB");
    setOption("demuxer-readahead-secs", "60");

    if (mpv_initialize(m_mpv) < 0) {
        qCritical("mpv: mpv_initialize falló");
        return;
    }
    mpv_request_log_messages(m_mpv, "warn");
    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "paused-for-cache", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "demuxer-cache-time", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "track-list", MPV_FORMAT_NONE);
    mpv_observe_property(m_mpv, 0, "aid", MPV_FORMAT_NONE);
    mpv_observe_property(m_mpv, 0, "sid", MPV_FORMAT_NONE);
    mpv_observe_property(m_mpv, 0, "hwdec-current", MPV_FORMAT_NONE);
    mpv_observe_property(m_mpv, 0, "video-params", MPV_FORMAT_NONE);
    mpv_set_wakeup_callback(m_mpv, onMpvWakeup, this);

    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *w) {
        if (w)
            connect(w, &QQuickWindow::sceneGraphInvalidated, this, &MpvItem::releaseRender, Qt::DirectConnection);
    });
}

void MpvItem::releaseRender()
{
    if (m_gl) {
        mpv_render_context_free(m_gl);
        m_gl = nullptr;
    }
}

MpvItem::~MpvItem()
{
    shutdown();
}

void MpvItem::shutdown()
{
    if (!m_mpv)
        return;
    // 1) El contexto de render se libera en el hilo de render (con el contexto GL activo), esperando sin bloquear el bucle de eventos.
    if (m_gl && window()) {
        auto done = std::make_shared<std::atomic_bool>(false);
        window()->scheduleRenderJob(QRunnable::create([this, done] {
            releaseRender();
            done->store(true);
        }), QQuickWindow::BeforeSynchronizingStage);
        window()->update();
        QElapsedTimer t;
        t.start();
        while (!done->load() && t.elapsed() < 3000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    // 2) Con mpv sin contexto de render, se destruye el handle. Si el contexto sigue vivo, destruir mpv abortaría el proceso: se omite.
    if (m_gl) {
        qWarning("mpv: el contexto de render no se liberó a tiempo; se omite mpv_terminate_destroy");
        return;
    }
    mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
    // En un hilo aparte: mpv desinicializa estado por hilo (COM en Windows) y no debe hacerlo en el hilo de la interfaz.
    mpv_handle *h = m_mpv;
    m_mpv = nullptr;
    std::thread([h] { mpv_terminate_destroy(h); }).join();
}

QQuickFramebufferObject::Renderer *MpvItem::createRenderer() const
{
    return new MpvRenderer(const_cast<MpvItem *>(this));
}

void MpvItem::doUpdate()
{
    update();
}

void MpvItem::setOption(const char *name, const char *value)
{
    const int rc = mpv_set_option_string(m_mpv, name, value);
    if (rc < 0)
        qWarning("mpv: opción %s=%s rechazada (%s)", name, value, mpv_error_string(rc));
}

QString MpvItem::getString(const char *name) const
{
    if (!m_mpv)
        return {};
    char *s = mpv_get_property_string(m_mpv, name);
    if (!s)
        return {};
    const QString out = QString::fromUtf8(s);
    mpv_free(s);
    return out;
}

void MpvItem::command(const QStringList &args)
{
    if (!m_mpv)
        return;
    QList<QByteArray> bytes;
    bytes.reserve(args.size());
    for (const QString &a : args)
        bytes.append(a.toUtf8());
    QList<const char *> argv;
    for (const QByteArray &b : std::as_const(bytes))
        argv.append(b.constData());
    argv.append(nullptr);
    const int rc = mpv_command_async(m_mpv, 0, argv.data());
    if (rc < 0)
        qWarning("mpv: comando %s falló (%s)", qPrintable(args.value(0)), mpv_error_string(rc));
}

void MpvItem::load(const QString &source, double start)
{
    setState(QStringLiteral("cargando"));
    m_errorLog.clear();
    if (!m_errorText.isEmpty()) {
        m_errorText.clear();
        emit errorTextChanged();
    }
    if (m_cached != 0) {
        m_cached = 0;
        emit cachedChanged();
    }
    if (start > 0)
        mpv_set_property_string(m_mpv, "start", QByteArray::number(start, 'f', 3).constData());
    else
        mpv_set_property_string(m_mpv, "start", "none");
    command({QStringLiteral("loadfile"), source});
}

void MpvItem::togglePause()
{
    command({QStringLiteral("cycle"), QStringLiteral("pause")});
}

void MpvItem::seek(double seconds, bool relative)
{
    command({QStringLiteral("seek"), QString::number(seconds, 'f', 3),
             relative ? QStringLiteral("relative+exact") : QStringLiteral("absolute+exact")});
}

void MpvItem::setVolume(double v)
{
    v = qBound(0.0, v, 150.0);
    mpv_set_property_async(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE, &v);
}

void MpvItem::setMuted(bool m)
{
    mpv_set_property_string(m_mpv, "mute", m ? "yes" : "no");
}

void MpvItem::setSubtitleStyle(double size, double pos)
{
    mpv_set_property_string(m_mpv, "sub-font-size", QByteArray::number(qBound(16.0, size, 120.0), 'f', 0).constData());
    mpv_set_property_string(m_mpv, "sub-pos", QByteArray::number(qBound(0.0, pos, 100.0), 'f', 0).constData());
}

QVariantMap MpvItem::techInfo() const
{
    QVariantMap m;
    m["archivo"] = getString("path");
    m["contenedor"] = getString("file-format");
    m["resolucion"] = getString("video-params/w") + QStringLiteral("×") + getString("video-params/h");
    m["videoCodec"] = getString("video-codec");
    m["fps"] = getString("container-fps");
    m["formatoPixel"] = getString("video-params/pixelformat");
    m["hwdec"] = getString("hwdec-current");
    m["audioCodec"] = getString("audio-codec-name");
    m["audioCanales"] = getString("audio-params/hr-channels");
    m["audioFrecuencia"] = getString("audio-params/samplerate");
    m["perdidos"] = getString("frame-drop-count");
    return m;
}

static QString settingsFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/window.ini");
}

QVariant MpvItem::setting(const QString &key, const QVariant &fallback) const
{
    QSettings st(settingsFile(), QSettings::IniFormat);
    return st.value(QStringLiteral("player/") + key, fallback);
}

void MpvItem::setSetting(const QString &key, const QVariant &value)
{
    QSettings st(settingsFile(), QSettings::IniFormat);
    st.setValue(QStringLiteral("player/") + key, value);
}

void MpvItem::setAudio(const QString &id)
{
    mpv_set_property_string(m_mpv, "aid", id.toUtf8().constData());
}

void MpvItem::setSubtitle(const QString &id)
{
    mpv_set_property_string(m_mpv, "sid", id.toUtf8().constData());
}

void MpvItem::addSubtitle(const QString &path)
{
    command({QStringLiteral("sub-add"), path, QStringLiteral("select")});
}

void MpvItem::stop()
{
    command({QStringLiteral("stop")});
}

void MpvItem::setState(const QString &s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged();
}

void MpvItem::refreshTracks()
{
    const QJsonDocument doc = QJsonDocument::fromJson(getString("track-list").toUtf8());
    QVariantList audio, subs;
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        QVariantMap t;
        t["id"] = QString::number(o["id"].toInt());
        t["lang"] = o["lang"].toString();
        t["title"] = o["title"].toString();
        t["codec"] = o["codec"].toString();
        t["external"] = o["external"].toBool();
        t["selected"] = o["selected"].toBool();
        const QString type = o["type"].toString();
        if (type == "audio")
            audio.append(t);
        else if (type == "sub")
            subs.append(t);
    }
    m_audio = audio;
    m_subs = subs;
    m_aid = getString("aid");
    m_sid = getString("sid");
    emit tracksChanged();
}

void MpvItem::refreshInfo()
{
    const QString hw = getString("hwdec-current");
    const QString codec = getString("video-codec");
    const QString w = getString("video-params/w");
    const QString h = getString("video-params/h");
    const QString acodec = getString("audio-codec-name");
    m_info = QStringLiteral("%1x%2 · %3 · audio %4 · hwdec: %5")
                 .arg(w, h, codec.section(' ', 0, 0), acodec, hw.isEmpty() ? QStringLiteral("no") : hw);
    emit infoChanged();
}

void MpvItem::processEvents()
{
    while (m_mpv) {
        mpv_event *ev = mpv_wait_event(m_mpv, 0);
        if (ev->event_id == MPV_EVENT_NONE)
            break;
        switch (ev->event_id) {
        case MPV_EVENT_PROPERTY_CHANGE: {
            auto *p = static_cast<mpv_event_property *>(ev->data);
            const QByteArray name = p->name;
            if (name == "time-pos" && p->format == MPV_FORMAT_DOUBLE) {
                m_position = *static_cast<double *>(p->data);
                emit positionChanged();
            } else if (name == "duration" && p->format == MPV_FORMAT_DOUBLE) {
                m_duration = *static_cast<double *>(p->data);
                emit durationChanged();
            } else if (name == "pause" && p->format == MPV_FORMAT_FLAG) {
                m_paused = *static_cast<int *>(p->data);
                setState(m_paused ? QStringLiteral("pausado") : QStringLiteral("reproduciendo"));
                emit pausedChanged();
            } else if (name == "paused-for-cache" && p->format == MPV_FORMAT_FLAG) {
                m_buffering = *static_cast<int *>(p->data);
                emit bufferingChanged();
            } else if (name == "mute" && p->format == MPV_FORMAT_FLAG) {
                m_muted = *static_cast<int *>(p->data);
                emit mutedChanged();
            } else if (name == "demuxer-cache-time" && p->format == MPV_FORMAT_DOUBLE) {
                m_cached = *static_cast<double *>(p->data);
                emit cachedChanged();
            } else if (name == "volume" && p->format == MPV_FORMAT_DOUBLE) {
                m_volume = *static_cast<double *>(p->data);
                emit volumeChanged();
            } else if (name == "track-list" || name == "aid" || name == "sid") {
                refreshTracks();
            } else if (name == "hwdec-current" || name == "video-params") {
                refreshInfo();
            }
            break;
        }
        case MPV_EVENT_START_FILE:
            setState(QStringLiteral("cargando"));
            break;
        case MPV_EVENT_PLAYBACK_RESTART:
            setState(m_paused ? QStringLiteral("pausado") : QStringLiteral("reproduciendo"));
            break;
        case MPV_EVENT_END_FILE: {
            auto *e = static_cast<mpv_event_end_file *>(ev->data);
            QString reason;
            switch (e->reason) {
            case MPV_END_FILE_REASON_EOF: reason = QStringLiteral("eof"); break;
            case MPV_END_FILE_REASON_ERROR: reason = QStringLiteral("error"); break;
            case MPV_END_FILE_REASON_STOP: reason = QStringLiteral("stop"); break;
            default: reason = QStringLiteral("otro"); break;
            }
            qInfo("mpv: fin de archivo (%s)%s", qPrintable(reason),
                  e->reason == MPV_END_FILE_REASON_ERROR ? qPrintable(QStringLiteral(": ") + QString::fromUtf8(mpv_error_string(e->error))) : "");
            if (reason == QStringLiteral("error")) {
                m_errorBase = QString::fromUtf8(mpv_error_string(e->error));
                m_errorText = m_errorBase;
                if (!m_errorLog.isEmpty())
                    m_errorText += QStringLiteral("\n\n") + m_errorLog.join(QLatin1Char('\n'));
                emit errorTextChanged();
            }
            setState(reason == QStringLiteral("error") ? QStringLiteral("error") : QStringLiteral("fin"));
            emit endOfFile(reason);
            break;
        }
        case MPV_EVENT_LOG_MESSAGE: {
            auto *m = static_cast<mpv_event_log_message *>(ev->data);
            qInfo("mpv[%s/%s] %s", m->prefix, m->level, QByteArray(m->text).trimmed().constData());
            if (m->log_level <= MPV_LOG_LEVEL_ERROR) {   // detalle técnico para la pantalla de error
                m_errorLog.append(QStringLiteral("[%1] %2").arg(QString::fromUtf8(m->prefix), QString::fromUtf8(m->text).trimmed()));
                while (m_errorLog.size() > 8)
                    m_errorLog.removeFirst();
                if (m_state == QStringLiteral("error")) {   // el detalle puede llegar después del evento de fin
                    m_errorText = m_errorBase + QStringLiteral("\n\n") + m_errorLog.join(QLatin1Char('\n'));
                    emit errorTextChanged();
                }
            }
            break;
        }
        case MPV_EVENT_SHUTDOWN:
            return;
        default:
            break;
        }
    }
}

QVariantMap MpvItem::stats() const
{
    QVariantMap s;
    static const char *props[] = {"hwdec-current", "video-codec", "audio-codec-name", "video-params/w", "video-params/h",
                                  "video-params/pixelformat", "estimated-vf-fps", "frame-drop-count",
                                  "decoder-frame-drop-count", "current-vo", "gpu-api", "time-pos", "duration",
                                  "container-fps", "audio-params/format", "audio-params/channel-count",
                                  "mpv-version", "mpv-configuration", "ffmpeg-version", "ffmpeg-configuration"};
    for (const char *p : props)
        s[QString::fromUtf8(p)] = getString(p);
    s["track-list"] = QJsonDocument::fromJson(getString("track-list").toUtf8()).array().toVariantList();
    return s;
}

bool MpvItem::writeStats(const QString &path, const QVariantMap &extra) const
{
    QVariantMap s = stats();
    for (auto it = extra.cbegin(); it != extra.cend(); ++it)
        s[it.key()] = it.value();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(QJsonObject::fromVariantMap(s)).toJson(QJsonDocument::Indented));
    return true;
}

bool MpvItem::grabWindowTo(const QString &path)
{
    return window() && window()->grabWindow().save(path);
}
