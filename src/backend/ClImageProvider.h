#pragma once
#include <QQuickAsyncImageProvider>

class BackendLauncher;

/**
 * `image://cl/<url TMDb codificada>`: resuelve las imágenes a través del backend (`/api/v1/image`), que usa la caché compartida
 * `images/` (índice de Flutter → cl_image_cache → descarga de TMDb). Mientras el backend arranca, las peticiones esperan; si el
 * backend no está disponible se descarga directamente de TMDb. La decodificación se hace ya reducida (`sourceSize`) en un hilo
 * del pool, nunca en el hilo de la interfaz.
 */
class ClImageProvider : public QQuickAsyncImageProvider
{
public:
    explicit ClImageProvider(BackendLauncher *backend);
    ~ClImageProvider() override;
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

private:
    BackendLauncher *m_backend;
    class QNetworkAccessManager *m_nam;
};
