#pragma once

#include <QImage>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>
#include <QtQml/qqmlregistration.h>

class ThumbWorker;

/**
 * Almacén de miniaturas servido a QML como `image://thumb/<generación>/<cubo>`. Compartido (estático) porque el proveedor de
 * imágenes lo pide el motor QML desde otro hilo y el generador (`Thumbnailer`) lo llena desde el hilo de la interfaz.
 */
class ThumbnailStore : public QQuickImageProvider
{
public:
    ThumbnailStore() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
    static void put(const QString &key, const QImage &image);
    static bool has(const QString &key);
    static void clear();
};

/**
 * Fotogramas de vista previa para la línea de tiempo del reproductor. Usa una SEGUNDA instancia de mpv, sin ventana ni audio,
 * que dibuja con el renderizador por software de libmpv directamente a un tamaño pequeño (320 px de ancho), en un hilo de
 * prioridad baja. Salta al fotograma clave más cercano (sin decodificar con precisión de fotograma) y guarda en memoria las
 * miniaturas ya generadas, por cubos de unos pocos segundos. Solo para archivos locales: con URLs (Modo Cliente) no hace nada.
 * Ver specs/02-reproductor-mpv.md.
 */
class Thumbnailer : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit Thumbnailer(QObject *parent = nullptr);
    ~Thumbnailer() override;

    /** Prepara la fuente de la película en reproducción (la instancia de mpv se crea al primer `request`). */
    Q_INVOKABLE void open(const QString &source);
    /** Libera la instancia de mpv y las miniaturas. */
    Q_INVOKABLE void close();
    /** Pide la miniatura del instante indicado (se agrupa en cubos de `kStep` segundos). Emite `ready()` al tenerla. */
    Q_INVOKABLE void request(double seconds);
    /** URL (`image://thumb/…`) de la miniatura de ese instante, o cadena vacía si todavía no está generada. */
    Q_INVOKABLE QString urlFor(double seconds) const;

    static constexpr double kStep = 5.0;

signals:
    void ready();

private:
    friend class ThumbWorker;
    void deliver(int generation, int bucket, const QImage &image);
    void stopWorker();

    ThumbWorker *m_worker = nullptr;
    QString m_source;
    int m_generation = 0;
    int m_lastBucket = -1;
};
