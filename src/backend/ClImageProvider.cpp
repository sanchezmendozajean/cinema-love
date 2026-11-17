#include "ClImageProvider.h"
#include "BackendLauncher.h"

#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QQuickTextureFactory>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

class Response : public QQuickImageResponse
{
public:
    Response(QNetworkAccessManager *nam, BackendLauncher *backend, const QString &src, const QSize &size)
        : m_nam(nam), m_backend(backend), m_src(src), m_size(size)
    {
    }

    /** Se llama ya en el hilo de la red/GUI (la petición llega desde el hilo de carga de imágenes de QML). */
    void start() { begin(); }

    QQuickTextureFactory *textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }
    QString errorString() const override { return m_error; }
    void cancel() override
    {
        QMetaObject::invokeMethod(this, [this]() {
            m_cancelled = true;
            if (m_reply) m_reply->abort();
        }, Qt::QueuedConnection);
    }

private:
    void begin()
    {
        const QString st = m_backend ? m_backend->state() : QStringLiteral("error");
        if (st == QLatin1String("listo")) {
            QUrl u(m_backend->baseUrl() + QStringLiteral("/api/v1/image"));
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("src"), m_src);
            q.addQueryItem(QStringLiteral("api_key"), m_backend->token());
            u.setQuery(q);
            fetch(u);
        } else if (st == QLatin1String("iniciando")) {
            // Aún no hay backend: se espera a que cambie de estado (listo o error) y se reintenta una sola vez.
            QPointer<Response> self(this);
            connect(m_backend, &BackendLauncher::stateChanged, this, [self]() {
                if (!self || self->m_started) return;
                const QString s = self->m_backend->state();
                if (s == QLatin1String("iniciando")) return;
                self->m_started = true;
                self->begin();
            });
            // Salvaguarda: si en 20 s no hay backend, se pasa a la descarga directa.
            QTimer::singleShot(20000, this, [self]() { if (self && !self->m_started) { self->m_started = true; self->fetch(QUrl(self->m_src)); } });
        } else {
            fetch(QUrl(m_src)); // backend con error: descarga directa de TMDb (sin caché compartida)
        }
    }

    void fetch(const QUrl &url)
    {
        m_started = true;
        QNetworkRequest req(url);
        req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
        m_reply = m_nam->get(req);
        connect(m_reply, &QNetworkReply::finished, this, [this]() {
            QNetworkReply *r = m_reply;
            m_reply = nullptr;
            const QByteArray bytes = r->readAll();
            const bool ok = r->error() == QNetworkReply::NoError;
            const QString err = r->errorString();
            r->deleteLater();
            if (m_cancelled || !ok || bytes.isEmpty()) {
                m_error = m_cancelled ? QStringLiteral("cancelada") : err;
                emit finished();
                return;
            }
            const QSize want = m_size;
            QPointer<Response> self(this);
            QThreadPool::globalInstance()->start([self, bytes, want]() {
                QBuffer buf;
                buf.setData(bytes);
                buf.open(QIODevice::ReadOnly);
                QImageReader reader(&buf);
                reader.setAutoTransform(true);
                const QSize orig = reader.size();
                if (want.width() > 0 && orig.width() > want.width()) // decodifica ya reducido (JPEG/PNG/WebP)
                    reader.setScaledSize(QSize(want.width(), qRound(double(orig.height()) * want.width() / orig.width())));
                const QImage img = reader.read();
                const QString e = img.isNull() ? reader.errorString() : QString();
                if (!self) return;
                QMetaObject::invokeMethod(self.data(), [self, img, e]() {
                    if (!self) return;
                    self->m_image = img;
                    self->m_error = e;
                    emit self->finished();
                }, Qt::QueuedConnection);
            });
        });
    }

    QNetworkAccessManager *m_nam;
    QPointer<BackendLauncher> m_backend;
    QString m_src, m_error;
    QSize m_size;
    QImage m_image;
    QNetworkReply *m_reply = nullptr;
    bool m_cancelled = false, m_started = false;
};

} // namespace

ClImageProvider::ClImageProvider(BackendLauncher *backend) : m_backend(backend), m_nam(new QNetworkAccessManager) {}

ClImageProvider::~ClImageProvider() { delete m_nam; }

QQuickImageResponse *ClImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    // El id llega como la URL de TMDb; si QML la dejó codificada se decodifica.
    const QString src = id.contains(QLatin1String("://")) ? id : QUrl::fromPercentEncoding(id.toUtf8());
    auto *r = new Response(m_nam, m_backend, src, requestedSize);
    // QNetworkAccessManager y el lanzador viven en el hilo principal: toda la lógica de red de la respuesta corre allí.
    r->moveToThread(m_nam->thread());
    QMetaObject::invokeMethod(r, [r]() { r->start(); }, Qt::QueuedConnection);
    return r;
}
