#include "HttpServer.h"

#include <QSslServer>

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QtConcurrent>
#include <QRegularExpression>
#include <QTcpSocket>

namespace {

const char *reason(int s)
{
    switch (s) {
    case 200: return "OK";
    case 204: return "No Content";
    case 206: return "Partial Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 416: return "Range Not Satisfiable";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    default: return "OK";
    }
}

constexpr int kMaxHeader = 64 * 1024;
constexpr int kMaxBody = 16 * 1024 * 1024;
constexpr qint64 kChunk = 256 * 1024;
constexpr qint64 kHighWater = 1024 * 1024;

} // namespace

QJsonValue HttpRequest::json(bool *ok) const
{
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(body, &pe);
    if (ok) *ok = pe.error == QJsonParseError::NoError;
    if (pe.error != QJsonParseError::NoError) return {};
    return d.isArray() ? QJsonValue(d.array()) : QJsonValue(d.object());
}

HttpResponse HttpResponse::json(const QJsonValue &v, int status)
{
    HttpResponse r;
    r.status = status;
    r.body = v.isArray() ? QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact) : QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact);
    r.headers.append({QByteArray("Content-Type"), QByteArray("application/json; charset=utf-8")});
    return r;
}

HttpResponse HttpResponse::error(int status, const QString &message)
{
    return json(QJsonObject{{"error", message}}, status);
}

HttpResponse HttpResponse::bytes(const QByteArray &data, const QByteArray &contentType, int status)
{
    HttpResponse r;
    r.status = status;
    r.body = data;
    r.headers.append({QByteArray("Content-Type"), contentType});
    return r;
}

HttpResponse HttpResponse::file(const QString &path, const QString &contentType)
{
    HttpResponse r;
    r.filePath = path;
    r.fileContentType = contentType;
    return r;
}

SseChannel::SseChannel(QTcpSocket *s) : QObject(s), m_sock(s)
{
    connect(s, &QTcpSocket::disconnected, this, &SseChannel::closed);
    auto *beat = new QTimer(this);   // latido: mantiene viva la conexión y detecta clientes caídos
    connect(beat, &QTimer::timeout, this, [this] { m_sock->write(": latido\n\n"); });
    beat->start(20000);
}

void SseChannel::send(const QString &event, const QJsonValue &data)
{
    const QByteArray json = data.isArray() ? QJsonDocument(data.toArray()).toJson(QJsonDocument::Compact) : QJsonDocument(data.toObject()).toJson(QJsonDocument::Compact);
    m_sock->write("event: " + event.toUtf8() + "\ndata: " + json + "\n\n");
}

ResponseWriter::ResponseWriter(QTcpSocket *s, QList<QPair<QByteArray, QByteArray>> extraHeaders, std::function<void(bool)> onEnd)
    : QObject(s), m_sock(s), m_extra(std::move(extraHeaders)), m_onEnd(std::move(onEnd))
{
    connect(s, &QTcpSocket::bytesWritten, this, [this] { emit drained(); });
    connect(s, &QTcpSocket::disconnected, this, [this] { m_gone = true; emit closed(); });
}

void ResponseWriter::begin(int status, const QList<QPair<QByteArray, QByteArray>> &headers, qint64 contentLength)
{
    QByteArray h = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason(status) + "\r\n";
    for (const auto &p : headers) h += p.first + ": " + p.second + "\r\n";
    for (const auto &p : m_extra) h += p.first + ": " + p.second + "\r\n";
    m_closeAfter = contentLength < 0;
    if (contentLength >= 0) h += "Content-Length: " + QByteArray::number(contentLength) + "\r\n";
    h += m_closeAfter ? "Connection: close\r\n\r\n" : "Connection: keep-alive\r\n\r\n";
    m_sock->write(h);
}

void ResponseWriter::write(const QByteArray &data)
{
    if (isOpen() && !data.isEmpty()) m_sock->write(data);
}

qint64 ResponseWriter::bytesToWrite() const { return m_sock->bytesToWrite(); }

void ResponseWriter::end()
{
    if (m_ended) return;
    m_ended = true;
    if (m_onEnd) m_onEnd(m_closeAfter);
}

/** Una conexión TCP: analiza peticiones (keep-alive) y escribe respuestas, enviando archivos por trozos con control de flujo. */
class HttpConnection : public QObject
{
public:
    HttpConnection(HttpServer *srv, QTcpSocket *s) : QObject(s), m_srv(srv), m_sock(s)
    {
        connect(s, &QTcpSocket::readyRead, this, &HttpConnection::onRead);
        connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        connect(s, &QTcpSocket::bytesWritten, this, &HttpConnection::pump);
        s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    }

private:
    void onRead()
    {
        m_buf += m_sock->readAll();
        process();
    }

    void process()
    {
        while (!m_streaming && !m_waiting) {
            const int end = m_buf.indexOf("\r\n\r\n");
            if (end < 0) {
                if (m_buf.size() > kMaxHeader) simple(HttpResponse::error(400, QStringLiteral("Solicitud inválida.")), true);
                return;
            }
            HttpRequest req;
            const QList<QByteArray> lines = m_buf.left(end).split('\n');
            const QList<QByteArray> start = lines.value(0).trimmed().split(' ');
            if (start.size() < 3) { simple(HttpResponse::error(400, QStringLiteral("Solicitud inválida.")), true); return; }
            req.method = QString::fromLatin1(start.at(0)).toUpper();
            const QByteArray target = start.at(1);
            const int qm = target.indexOf('?');
            req.path = QString::fromUtf8(QByteArray::fromPercentEncoding(qm < 0 ? target : target.left(qm)));
            while (req.path.size() > 1 && req.path.endsWith(QLatin1Char('/'))) req.path.chop(1);
            if (qm >= 0) req.query = QUrlQuery(QString::fromUtf8(target.mid(qm + 1)));
            for (int i = 1; i < lines.size(); ++i) {
                const QByteArray l = lines.at(i).trimmed();
                const int c = l.indexOf(':');
                if (c > 0) req.headers.insert(QString::fromLatin1(l.left(c)).toLower(), QString::fromUtf8(l.mid(c + 1).trimmed()));
            }
            const qint64 len = req.headers.value(QStringLiteral("content-length")).toLongLong();
            if (len > kMaxBody) { simple(HttpResponse::error(413, QStringLiteral("Cuerpo demasiado grande.")), true); return; }
            if (m_buf.size() < end + 4 + len) return;   // falta cuerpo
            req.body = m_buf.mid(end + 4, len);
            m_buf.remove(0, end + 4 + len);
            req.peer = m_sock->peerAddress().toString();
            const bool close = req.headers.value(QStringLiteral("connection")).toLower() == QLatin1String("close");
            handle(req, close);
            if (close) return;
        }
    }

    void handle(HttpRequest &req, bool close)
    {
        m_closeAfter = close;
        m_waiting = true;
        QPointer<HttpConnection> self(this);
        auto done = [self, req](HttpResponse res) {
            if (self) self->finish(req, std::move(res));
        };
        if (req.method == QLatin1String("OPTIONS")) {
            HttpResponse r;
            r.status = 204;
            done(r);
        } else {
            m_srv->dispatch(req, done);
        }
    }

    void finish(const HttpRequest &req, HttpResponse res)
    {
        const QString origin = req.headers.value(QStringLiteral("origin"));
        // CORS abierto: la API no usa cookies; lo que protege es la credencial.
        res.header("Access-Control-Allow-Origin", origin.isEmpty() ? QByteArray("*") : origin.toUtf8());
        res.header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.header("Access-Control-Allow-Headers", "X-Api-Key, Content-Type, Range");
        res.header("Access-Control-Expose-Headers", "Content-Range, Accept-Ranges");
        if (!origin.isEmpty()) res.header("Vary", "Origin");
        m_waiting = false;
        if (res.sse) {
            QByteArray head = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\n";
            for (const auto &p : res.headers) head += p.first + ": " + p.second + "\r\n";
            m_sock->write(head + "\r\n");
            m_streaming = true;   // la conexión queda dedicada al stream
            res.sse(new SseChannel(m_sock));
            return;
        }
        if (res.stream) {
            m_streaming = true;   // la conexión queda dedicada a esta respuesta hasta que termine
            auto *w = new ResponseWriter(m_sock, res.headers, [this](bool closeAfter) {
                m_streaming = false;
                if (closeAfter || m_closeAfter) m_sock->disconnectFromHost();
                else if (!m_buf.isEmpty()) QMetaObject::invokeMethod(this, [this] { process(); }, Qt::QueuedConnection);
            });
            res.stream(w);
            return;
        }
        if (!res.filePath.isEmpty()) sendFile(req, res);
        else write(res, res.body.size(), req.method == QLatin1String("HEAD") ? QByteArray() : res.body);
        if (!m_streaming && !m_closeAfter && !m_buf.isEmpty()) QMetaObject::invokeMethod(this, [this] { process(); }, Qt::QueuedConnection);
    }

    void head(const HttpResponse &res, qint64 length, const QByteArray &extra = {})
    {
        QByteArray h = "HTTP/1.1 " + QByteArray::number(res.status) + ' ' + reason(res.status) + "\r\n";
        for (const auto &p : res.headers) h += p.first + ": " + p.second + "\r\n";
        h += "Content-Length: " + QByteArray::number(length) + "\r\n";
        h += extra;
        h += m_closeAfter ? "Connection: close\r\n\r\n" : "Connection: keep-alive\r\n\r\n";
        m_sock->write(h);
    }

    void write(const HttpResponse &res, qint64 length, const QByteArray &body)
    {
        head(res, length);
        if (!body.isEmpty()) m_sock->write(body);
        if (m_closeAfter) m_sock->disconnectFromHost();
    }

    void simple(const HttpResponse &res, bool close)
    {
        m_closeAfter = close;
        write(res, res.body.size(), res.body);
        m_buf.clear();
    }

    void sendFile(const HttpRequest &req, HttpResponse &res)
    {
        auto *f = new QFile(res.filePath, this);
        if (!f->open(QIODevice::ReadOnly)) {
            delete f;
            simple(HttpResponse::error(404, QStringLiteral("El archivo no se encuentra en la ruta actual.")), m_closeAfter);
            return;
        }
        const qint64 size = f->size();
        qint64 from = 0, to = size - 1;
        int status = 200;
        QByteArray extra = "Accept-Ranges: bytes\r\n";
        // Un solo rango «bytes=a-b»; si el encabezado no es de ese formato se sirve el archivo completo.
        static const QRegularExpression rangeRe(QStringLiteral("^bytes=(\\d*)-(\\d*)$"));
        const QRegularExpressionMatch rm = rangeRe.match(req.headers.value(QStringLiteral("range")).trimmed());
        if (rm.hasMatch() && !(rm.captured(1).isEmpty() && rm.captured(2).isEmpty())) {
            qint64 rs, re;
            if (rm.captured(1).isEmpty()) {
                const qint64 n = rm.captured(2).toLongLong();
                rs = n == 0 ? size : qMax<qint64>(size - n, 0);   // «últimos 0 bytes»: no satisfacible
                re = size - 1;
            } else {
                rs = rm.captured(1).toLongLong();
                re = rm.captured(2).isEmpty() ? size - 1 : qMin(rm.captured(2).toLongLong(), size - 1);
            }
            if (rs >= size || rs > re) {
                delete f;
                HttpResponse r416;
                r416.status = 416;
                r416.header("Content-Range", "bytes */" + QByteArray::number(size));
                r416.header("Accept-Ranges", "bytes");
                r416.header("Content-Type", res.fileContentType.toUtf8());
                write(r416, 0, {});
                return;
            }
            from = rs;
            to = re;
            status = 206;
            extra += "Content-Range: bytes " + QByteArray::number(from) + '-' + QByteArray::number(to) + '/' + QByteArray::number(size) + "\r\n";
        }
        HttpResponse r;
        r.status = status;
        r.headers = res.headers;
        r.headers.append({QByteArray("Content-Type"), res.fileContentType.toUtf8()});
        head(r, to - from + 1, extra);
        if (req.method == QLatin1String("HEAD")) { delete f; if (m_closeAfter) m_sock->disconnectFromHost(); return; }
        f->seek(from);
        m_file = f;
        m_remaining = to - from + 1;
        m_streaming = true;
        pump();
    }

    void pump()
    {
        if (!m_streaming || !m_file) return;
        while (m_remaining > 0 && m_sock->bytesToWrite() < kHighWater) {
            const QByteArray chunk = m_file->read(qMin(kChunk, m_remaining));
            if (chunk.isEmpty()) { m_remaining = 0; break; }
            m_remaining -= chunk.size();
            m_sock->write(chunk);
        }
        if (m_remaining <= 0) {
            m_file->deleteLater();
            m_file = nullptr;
            m_streaming = false;
            if (m_closeAfter) m_sock->disconnectFromHost();
            else if (!m_buf.isEmpty()) QMetaObject::invokeMethod(this, [this] { process(); }, Qt::QueuedConnection);
        }
    }

    HttpServer *m_srv;
    QTcpSocket *m_sock;
    QByteArray m_buf;
    QFile *m_file = nullptr;
    qint64 m_remaining = 0;
    bool m_streaming = false;
    bool m_waiting = false;   // respuesta asíncrona en curso: no se procesa la siguiente petición
    bool m_closeAfter = false;
};

HttpServer::HttpServer(QObject *parent) : QObject(parent) {}

HttpServer::~HttpServer() { stop(); }

void HttpServer::route(const QString &method, const QString &pattern, HttpHandler h)
{
    m_routes.append({method.toUpper(), pattern.split(QLatin1Char('/'), Qt::SkipEmptyParts), std::move(h), nullptr});
}

void HttpServer::routePrefix(const QString &method, const QString &prefix, AsyncHandler h)
{
    m_prefixRoutes.append({method.toUpper(), prefix, std::move(h)});
}

void HttpServer::routeAsync(const QString &method, const QString &pattern, AsyncHandler h)
{
    m_routes.append({method.toUpper(), pattern.split(QLatin1Char('/'), Qt::SkipEmptyParts), nullptr, std::move(h)});
}

void HttpServer::dispatch(HttpRequest &req, const std::function<void(HttpResponse)> &done)
{
    const QStringList parts = req.path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const Route &r : std::as_const(m_routes)) {
        if (r.parts.size() != parts.size()) continue;
        QMap<QString, QString> params;
        bool ok = true;
        for (int i = 0; i < parts.size() && ok; ++i) {
            if (r.parts.at(i).startsWith(QLatin1Char(':'))) params.insert(r.parts.at(i).mid(1), QUrl::fromPercentEncoding(parts.at(i).toUtf8()));
            else ok = r.parts.at(i) == parts.at(i);
        }
        if (!ok) continue;
        if (r.method != req.method && !(req.method == QLatin1String("HEAD") && r.method == QLatin1String("GET"))) continue;
        req.params = params;
        HttpResponse res;
        if (m_guard && !m_guard(req, res)) { done(res); return; }
        try {
            if (r.async) r.async(req, done);
            else done(r.handler(req));
        } catch (const std::exception &e) {
            qWarning("servidor: excepción en %s: %s", qPrintable(req.path), e.what());
            done(HttpResponse::error(500, QStringLiteral("Error interno del servidor.")));
        }
        return;
    }
    for (const PrefixRoute &p : std::as_const(m_prefixRoutes)) {
        if (!req.path.startsWith(p.prefix) || (!p.method.isEmpty() && p.method != req.method)) continue;
        HttpResponse res;
        if (m_guard && !m_guard(req, res)) { done(res); return; }
        try {
            p.handler(req, done);
        } catch (const std::exception &e) {
            qWarning("servidor: excepción en %s: %s", qPrintable(req.path), e.what());
            done(HttpResponse::error(500, QStringLiteral("Error interno del servidor.")));
        }
        return;
    }
    done(HttpResponse::error(404, QStringLiteral("Ruta no encontrada.")));
}

quint16 HttpServer::listen(const QString &host, quint16 port)
{
    moveToThread(&m_thread);
    m_thread.start();
    QMetaObject::invokeMethod(this, [this, host, port] {
        if (m_threadInit) m_threadInit();
        if (m_useTls) {
            auto *ssl = new QSslServer(this);
            ssl->setSslConfiguration(m_tls);
            // Los fallos de handshake (clientes que no aceptan el certificado autofirmado) son normales: no se registran.
            m_server = ssl;
        } else {
            m_server = new QTcpServer(this);
        }
        auto accept = [this] {
            while (QTcpSocket *s = m_server->nextPendingConnection()) new HttpConnection(this, s);
        };
        // QSslServer avisa con pendingConnectionAvailable cuando el handshake TLS ya terminó; QTcpServer con newConnection.
        if (m_useTls) connect(m_server, &QTcpServer::pendingConnectionAvailable, this, accept);
        else connect(m_server, &QTcpServer::newConnection, this, accept);
        if (m_server->listen(QHostAddress(host), port)) m_port = m_server->serverPort();
    }, Qt::BlockingQueuedConnection);
    return m_port;
}

void HttpServer::stop()
{
    if (!m_thread.isRunning()) return;
    QMetaObject::invokeMethod(this, [this] {
        if (m_server) { m_server->close(); delete m_server; m_server = nullptr; }
        if (m_threadDone) m_threadDone();
    }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait(3000);
}

void HttpServer::runBlocking(std::function<HttpResponse()> fn, std::function<void(HttpResponse)> done)
{
    QPointer<HttpServer> self(this);
    (void)QtConcurrent::run([self, fn = std::move(fn), done = std::move(done)] {
        HttpResponse res;
        try {
            res = fn();
        } catch (const std::exception &e) {
            qWarning("servidor: error en trabajo bloqueante: %s", e.what());
            res = HttpResponse::error(500, QStringLiteral("Error interno del servidor."));
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [done, res = std::move(res)] { done(res); }, Qt::QueuedConnection);
    });
}
