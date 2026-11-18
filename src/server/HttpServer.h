#pragma once
#include <QHash>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QSet>
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

struct HttpResponse {
    int status = 200;
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray body;
    QString filePath;                     // si no está vacío, se envía este archivo (con soporte de Range)
    QString fileContentType;

    static HttpResponse json(const QJsonValue &v, int status = 200);
    static HttpResponse error(int status, const QString &message);
    static HttpResponse bytes(const QByteArray &data, const QByteArray &contentType, int status = 200);
    static HttpResponse file(const QString &path, const QString &contentType);
    HttpResponse &header(const QByteArray &k, const QByteArray &v) { headers.append({k, v}); return *this; }
};

using HttpHandler = std::function<HttpResponse(HttpRequest &)>;

class HttpServer : public QObject
{
    Q_OBJECT
public:
    explicit HttpServer(QObject *parent = nullptr);
    ~HttpServer() override;

    /** Registra una ruta: pattern como «/api/v1/items/:id». */
    void route(const QString &method, const QString &pattern, HttpHandler h);
    /** Se llama antes de cada manejador; devuelve false (y rellena la respuesta) si la petición no está autorizada. */
    void setGuard(std::function<bool(HttpRequest &, HttpResponse &)> g) { m_guard = std::move(g); }
    /** Se ejecuta en el hilo del servidor al arrancar (para abrir la base de datos de ese hilo). */
    void setThreadInit(std::function<void()> f) { m_threadInit = std::move(f); }
    void setThreadDone(std::function<void()> f) { m_threadDone = std::move(f); }

    /** Escucha en host:port (0 = puerto libre). Devuelve el puerto o 0 si falló. Bloquea hasta que el hilo está listo. */
    quint16 listen(const QString &host, quint16 port);
    void stop();

private:
    friend class HttpConnection;
    struct Route { QString method; QStringList parts; HttpHandler handler; };
    HttpResponse dispatch(HttpRequest &req);

    QList<Route> m_routes;
    std::function<bool(HttpRequest &, HttpResponse &)> m_guard;
    std::function<void()> m_threadInit, m_threadDone;
    QThread m_thread;
    QTcpServer *m_server = nullptr;
    quint16 m_port = 0;
};
