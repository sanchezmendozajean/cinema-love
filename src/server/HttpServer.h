#pragma once
#include <QHash>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QSet>
#include <QSslConfiguration>
#include <QTcpServer>
#include <QThread>
#include <QUrlQuery>
#include <functional>

/**
 * Servidor HTTP/1.1 propio (QtHttpServer no forma parte de la instalación de Qt): rutas con parámetros, CORS, keep-alive y envío de
 * archivos con `Range`. Corre en su propio hilo; los manejadores se ejecutan en ese hilo (la base de datos usa una conexión por hilo).
 */
struct HttpRequest {
    QString method;
    QString path;                         // sin query, sin «/» final
    QUrlQuery query;
    QMap<QString, QString> headers;       // claves en minúsculas
    QByteArray body;
    QMap<QString, QString> params;        // parámetros de la ruta (:id)
    QString peer;
    // Lo rellena el autenticador
    QString principal;                    // admin | token | usuario
    QString userId;                       // vacío = admin local
    QString userName;
    QString sessionToken;

    QString q(const QString &key) const { return query.queryItemValue(key, QUrl::FullyDecoded); }
    bool hasQ(const QString &key) const { return query.hasQueryItem(key); }
    QJsonValue json(bool *ok = nullptr) const;   // cuerpo interpretado como JSON (objeto o lista)
};

/** Canal de eventos en vivo (Server-Sent Events) sobre una conexión HTTP que se mantiene abierta. Vive en el hilo del servidor. */
class SseChannel : public QObject
{
    Q_OBJECT
public:
    explicit SseChannel(QTcpSocket *s);
    /** Envía `event: <name>` + `data: <json>`. */
    void send(const QString &event, const QJsonValue &data);
signals:
    void closed();
private:
    QTcpSocket *m_sock;
};

/**
 * Escritor de una respuesta transmitida (el cuerpo llega por trozos, p. ej. un video que se reenvía desde otro servidor). `contentLength` < 0 =
 * longitud desconocida (se cierra la conexión al terminar). Avisa con `drained` cuando el socket tiene hueco y con `closed` si el cliente se va.
 */
class ResponseWriter : public QObject
{
    Q_OBJECT
public:
    ResponseWriter(QTcpSocket *s, QList<QPair<QByteArray, QByteArray>> extraHeaders, std::function<void(bool /*closeAfter*/)> onEnd);
    void begin(int status, const QList<QPair<QByteArray, QByteArray>> &headers, qint64 contentLength);
    void write(const QByteArray &data);
    void end();
    qint64 bytesToWrite() const;
    bool isOpen() const { return !m_ended && !m_gone; }
signals:
    void drained();
    void closed();
private:
    QTcpSocket *m_sock;
    QList<QPair<QByteArray, QByteArray>> m_extra;
    std::function<void(bool)> m_onEnd;
    bool m_ended = false, m_gone = false, m_closeAfter = false;
};

struct HttpResponse {
    int status = 200;
    std::function<void(ResponseWriter *)> stream;   // si está definido, el cuerpo lo escribe quien responda (transmisión por trozos)
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray body;
    std::function<void(SseChannel *)> sse; // si está definido, la conexión pasa a ser un stream de eventos en vivo
    QString filePath;                     // si no está vacío, se envía este archivo (con soporte de Range)
    QString fileContentType;

    static HttpResponse json(const QJsonValue &v, int status = 200);
    static HttpResponse error(int status, const QString &message);
    static HttpResponse bytes(const QByteArray &data, const QByteArray &contentType, int status = 200);
    static HttpResponse file(const QString &path, const QString &contentType);
    HttpResponse &header(const QByteArray &k, const QByteArray &v) { headers.append({k, v}); return *this; }
};

using HttpHandler = std::function<HttpResponse(HttpRequest &)>;
/** Manejador asíncrono: llama a `done` (en el hilo del servidor) cuando la respuesta está lista, p. ej. tras una petición de red. */
using AsyncHandler = std::function<void(HttpRequest &, std::function<void(HttpResponse)>)>;

class HttpServer : public QObject
{
    Q_OBJECT
public:
    explicit HttpServer(QObject *parent = nullptr);
    ~HttpServer() override;

    /** Registra una ruta: pattern como «/api/v1/items/:id». */
    void route(const QString &method, const QString &pattern, HttpHandler h);
    void routeAsync(const QString &method, const QString &pattern, AsyncHandler h);
    /** Ruta por prefijo («/api/v1/»): captura todo lo que empiece así y no coincida con una ruta exacta. `method` vacío = cualquiera. */
    void routePrefix(const QString &method, const QString &prefix, AsyncHandler h);
    /** Ejecuta `fn` en un hilo de trabajo (red, disco…) y entrega su resultado a `done` en el hilo del servidor. */
    void runBlocking(std::function<HttpResponse()> fn, std::function<void(HttpResponse)> done);
    /** Se llama antes de cada manejador; devuelve false (y rellena la respuesta) si la petición no está autorizada. */
    void setGuard(std::function<bool(HttpRequest &, HttpResponse &)> g) { m_guard = std::move(g); }
    std::function<bool(HttpRequest &, HttpResponse &)> guard() const { return m_guard; }
    /** Se ejecuta en el hilo del servidor al arrancar (para abrir la base de datos de ese hilo). */
    void setThreadInit(std::function<void()> f) { m_threadInit = std::move(f); }
    void setThreadDone(std::function<void()> f) { m_threadDone = std::move(f); }
    /** Con una configuración TLS (certificado y clave) el servidor atiende HTTPS en lugar de HTTP. Se llama antes de `listen`. */
    void setTls(const QSslConfiguration &c) { m_tls = c; m_useTls = true; }

    /** Escucha en host:port (0 = puerto libre). Devuelve el puerto o 0 si falló. Bloquea hasta que el hilo está listo. */
    quint16 listen(const QString &host, quint16 port);
    void stop();

private:
    friend class HttpConnection;
    struct Route { QString method; QStringList parts; HttpHandler handler; AsyncHandler async; };
    void dispatch(HttpRequest &req, const std::function<void(HttpResponse)> &done);

    struct PrefixRoute { QString method; QString prefix; AsyncHandler handler; };
    QList<PrefixRoute> m_prefixRoutes;
    QList<Route> m_routes;
    std::function<bool(HttpRequest &, HttpResponse &)> m_guard;
    std::function<void()> m_threadInit, m_threadDone;
    friend class HttpConnection;
    QThread m_thread;
    QTcpServer *m_server = nullptr;
    QSslConfiguration m_tls;
    bool m_useTls = false;
    quint16 m_port = 0;
};
