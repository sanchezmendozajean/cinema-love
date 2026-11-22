#include "Thumbnailer.h"

#include <QCache>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QWaitCondition>
#include <atomic>
#include <mpv/client.h>
#include <mpv/render.h>

// ---------------------------------------------------------------------------------------------------------------------
// Almacén de miniaturas
// ---------------------------------------------------------------------------------------------------------------------
namespace {
QMutex g_storeMutex;
QCache<QString, QImage> &store()
{
    static QCache<QString, QImage> cache(240);   // ~240 miniaturas de 320×180 (≈ 55 MB como máximo)
    return cache;
}
} // namespace

void ThumbnailStore::put(const QString &key, const QImage &image)
{
    QMutexLocker l(&g_storeMutex);
    store().insert(key, new QImage(image), 1);
}

bool ThumbnailStore::has(const QString &key)
{
    QMutexLocker l(&g_storeMutex);
    return store().contains(key);
}

void ThumbnailStore::clear()
{
    QMutexLocker l(&g_storeMutex);
    store().clear();
}

QImage ThumbnailStore::requestImage(const QString &id, QSize *size, const QSize &)
{
    QImage img;
    {
        QMutexLocker l(&g_storeMutex);
        if (const QImage *p = store().object(id))
            img = *p;
    }
    if (size)
        *size = img.size();
    return img;
}

// ---------------------------------------------------------------------------------------------------------------------
// Hilo generador: una instancia de mpv sin ventana ni audio que dibuja por software a tamaño de miniatura
// ---------------------------------------------------------------------------------------------------------------------
class ThumbWorker : public QThread
{
public:
    explicit ThumbWorker(Thumbnailer *owner) : m_owner(owner) {}

    void setSource(const QString &source, int generation)
    {
        QMutexLocker l(&m_m);
        m_source = source;
        m_generation = generation;
        m_srcPending = true;
        m_req = -1;
        m_dirty = true;
        m_cv.wakeAll();
    }
    void requestTime(double t)
    {
        QMutexLocker l(&m_m);
        m_req = t;
        m_dirty = true;
        m_cv.wakeAll();
    }
    void stop()
    {
        QMutexLocker l(&m_m);
        m_quit = true;
        m_dirty = true;
        m_cv.wakeAll();
    }

protected:
    void run() override
    {
        QThread::currentThread()->setPriority(QThread::LowPriority);   // no compite con la reproducción
        mpv_handle *h = mpv_create();
        if (!h)
            return;
        auto opt = [h](const char *k, const char *v) { mpv_set_option_string(h, k, v); };
        opt("vo", "libmpv");
        opt("hwdec", "no");                 // sin GPU: no compite con el video en reproducción
        opt("ao", "null");
        opt("aid", "no");
        opt("sid", "no");
        opt("sub-auto", "no");
        opt("audio-display", "no");
        opt("pause", "yes");
        opt("keep-open", "yes");
        opt("hr-seek", "no");               // fotograma clave más cercano: rápido
        opt("cache", "yes");
        opt("demuxer-max-bytes", "16MiB");
        opt("demuxer-max-back-bytes", "1MiB");
        opt("demuxer-readahead-secs", "1");
        opt("vd-lavc-threads", "2");
        opt("vd-lavc-skiploopfilter", "all");
        opt("vd-lavc-fast", "yes");
        opt("input-default-bindings", "no");
        opt("input-vo-keyboard", "no");
        opt("osc", "no");
        opt("osd-level", "0");
        opt("load-scripts", "no");
        opt("config", "no");
        opt("resume-playback", "no");
        opt("save-position-on-quit", "no");
        opt("terminal", "no");
        if (mpv_initialize(h) < 0) {
            mpv_terminate_destroy(h);
            return;
        }
        mpv_render_context *rc = nullptr;
        mpv_render_param cp[] = {{MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_SW)}, {MPV_RENDER_PARAM_INVALID, nullptr}};
        if (mpv_render_context_create(&rc, h, cp) < 0) {
            qWarning("Thumbnailer: este libmpv no admite el render por software; sin vista previa.");
            mpv_terminate_destroy(h);
            return;
        }

        bool loaded = false;
        for (;;) {
            QString src;
            double t = -1;
            int gen = 0;
            bool newSrc = false;
            {
                QMutexLocker l(&m_m);
                if (!m_quit && !m_dirty)
                    m_cv.wait(&m_m, 250);
                if (m_quit)
                    break;
                newSrc = m_srcPending;
                src = m_source;
                gen = m_generation;
                t = m_req;
                m_srcPending = false;
                m_req = -1;
                m_dirty = false;
            }
            drain(h);
            if (newSrc) {
                loaded = false;
                if (!src.isEmpty()) {
                    const QByteArray s = src.toUtf8();
                    const char *args[] = {"loadfile", s.constData(), "replace", nullptr};
                    if (mpv_command(h, args) >= 0)
                        loaded = waitFor(h, MPV_EVENT_FILE_LOADED, 4000);
                }
            }
            if (t < 0 || !loaded)
                continue;
            const QByteArray ts = QByteArray::number(t, 'f', 2);
            const char *args[] = {"seek", ts.constData(), "absolute+keyframes", nullptr};
            mpv_render_context_update(rc);   // descarta avisos de fotogramas anteriores
            if (mpv_command(h, args) < 0 || !waitFor(h, MPV_EVENT_PLAYBACK_RESTART, 2500))
                continue;
            for (int i = 0; i < 15 && !abortRequested(); ++i) {   // el fotograma llega un instante después del reinicio
                if (mpv_render_context_update(rc) & MPV_RENDER_UPDATE_FRAME)
                    break;
                QThread::msleep(15);
            }
            if (abortRequested())
                continue;
            QImage img;
            if (grab(h, rc, img)) {
                const int bucket = qRound(t / Thumbnailer::kStep);
                QMetaObject::invokeMethod(m_owner, [o = m_owner, gen, bucket, img]() { o->deliver(gen, bucket, img); }, Qt::QueuedConnection);
            }
        }
        mpv_render_context_free(rc);
        mpv_terminate_destroy(h);
    }

private:
    bool abortRequested()
    {
        QMutexLocker l(&m_m);
        return m_quit || m_dirty;   // llegó otra petición o se cerró: lo pendiente ya no interesa
    }

    static void drain(mpv_handle *h)
    {
        while (mpv_wait_event(h, 0)->event_id != MPV_EVENT_NONE) {}
    }

    /** Espera un evento de mpv; false por tiempo, fin de archivo, cierre o una petición más reciente. */
    bool waitFor(mpv_handle *h, mpv_event_id id, int timeoutMs)
    {
        QElapsedTimer tm;
        tm.start();
        while (tm.elapsed() < timeoutMs && !abortRequested()) {
            const mpv_event *e = mpv_wait_event(h, 0.05);
            if (e->event_id == id)
                return true;
            if (e->event_id == MPV_EVENT_END_FILE || e->event_id == MPV_EVENT_SHUTDOWN)
                return false;
        }
        return false;
    }

    /** Dibuja el fotograma actual a 320 px de ancho con el renderizador por software de libmpv. */
    static bool grab(mpv_handle *h, mpv_render_context *rc, QImage &out)
    {
        int64_t dw = 0, dh = 0;
        if (mpv_get_property(h, "dwidth", MPV_FORMAT_INT64, &dw) < 0 || mpv_get_property(h, "dheight", MPV_FORMAT_INT64, &dh) < 0 || dw <= 0 || dh <= 0)
            return false;   // sin video (p. ej. un archivo de audio)
        const int w = 320;
        const int hh = qBound(40, qRound(w * double(dh) / double(dw)), 480);
        QImage img(w, hh, QImage::Format_RGB32);
        int size[2] = {w, hh};
        size_t stride = size_t(img.bytesPerLine());
        const char *fmt = "bgr0";   // = QImage::Format_RGB32 en little-endian
        mpv_render_param p[] = {{MPV_RENDER_PARAM_SW_SIZE, size},
                                {MPV_RENDER_PARAM_SW_FORMAT, const_cast<char *>(fmt)},
                                {MPV_RENDER_PARAM_SW_STRIDE, &stride},
                                {MPV_RENDER_PARAM_SW_POINTER, img.bits()},
                                {MPV_RENDER_PARAM_INVALID, nullptr}};
        if (mpv_render_context_render(rc, p) < 0)
            return false;
        out = img;
        return true;
    }

    Thumbnailer *m_owner;
    QMutex m_m;
    QWaitCondition m_cv;
    QString m_source;
    int m_generation = 0;
    double m_req = -1;
    bool m_srcPending = false;
    bool m_dirty = false;
    bool m_quit = false;
};

// ---------------------------------------------------------------------------------------------------------------------
// Thumbnailer
// ---------------------------------------------------------------------------------------------------------------------
Thumbnailer::Thumbnailer(QObject *parent) : QObject(parent) {}

Thumbnailer::~Thumbnailer()
{
    stopWorker();
}

void Thumbnailer::stopWorker()
{
    if (!m_worker)
        return;
    m_worker->stop();
    m_worker->wait(5000);
    delete m_worker;
    m_worker = nullptr;
}

void Thumbnailer::open(const QString &source)
{
    close();
    // Solo archivos locales: con una URL (Modo Cliente) la segunda instancia abriría otra conexión de red.
    if (source.isEmpty() || source.startsWith(QLatin1String("http://")) || source.startsWith(QLatin1String("https://")))
        return;
    m_source = source;
}

void Thumbnailer::close()
{
    stopWorker();
    m_source.clear();
    m_lastBucket = -1;
    ++m_generation;
    ThumbnailStore::clear();
}

void Thumbnailer::request(double seconds)
{
    if (m_source.isEmpty() || seconds < 0)
        return;
    const int bucket = qRound(seconds / kStep);
    if (ThumbnailStore::has(QStringLiteral("%1/%2").arg(m_generation).arg(bucket))) {
        m_lastBucket = bucket;
        emit ready();
        return;
    }
    if (bucket == m_lastBucket)
        return;
    m_lastBucket = bucket;
    if (!m_worker) {
        m_worker = new ThumbWorker(this);
        m_worker->setSource(m_source, m_generation);
        m_worker->start();
    }
    m_worker->requestTime(bucket * kStep);
}

QString Thumbnailer::urlFor(double seconds) const
{
    const QString key = QStringLiteral("%1/%2").arg(m_generation).arg(qRound(seconds / kStep));
    return ThumbnailStore::has(key) ? QStringLiteral("image://thumb/") + key : QString();
}

void Thumbnailer::deliver(int generation, int bucket, const QImage &image)
{
    if (generation != m_generation)
        return;   // llegó tarde: ya se abrió otra película
    ThumbnailStore::put(QStringLiteral("%1/%2").arg(generation).arg(bucket), image);
    emit ready();
}
