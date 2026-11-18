#pragma once
#include <QHash>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QUdpSocket>

/**
 * Anuncio y descubrimiento de servidores CinemaLove en la red local con mDNS (RFC 6762), sin dependencias: servicio `_cinemalove._tcp`
 * con TXT `v`, `id`, `name`, `tls` (specs/heredadas/03 §4). Tiene su propio hilo; los métodos públicos se pueden llamar desde cualquiera.
 */
class Mdns : public QObject
{
    Q_OBJECT
public:
    struct Found {
        QString id, name, host;   // host = IPv4 si se conoce
        int port = 0;
        bool tls = false;
    };

    Mdns();
    ~Mdns() override;

    /** Empieza a anunciar (o actualiza el anuncio). `txt` lleva `v`, `id`, `name`, `tls`. */
    void publish(const QString &instanceName, int port, const QMap<QString, QString> &txt);
    /** Deja de anunciar (envía el aviso de despedida). */
    void unpublish();
    /** Empieza a buscar servidores; `servers()` devuelve los vistos hasta ahora. */
    void startBrowse();
    void stopBrowse();
    QList<Found> servers() const;
    bool announcing() const;

private:
    struct Instance { QString host; int port = 0; QMap<QString, QString> txt; qint64 seen = 0; bool ptr = false; };
    struct Rr { QStringList name; quint16 type = 0, cls = 0; quint32 ttl = 0; QByteArray data; QStringList target; quint16 port = 0; };

    void ensureSocket();            // en el hilo propio
    void closeSocketIfIdle();
    void onReadyRead();
    void sendQuery();
    void sendAnnouncement(bool goodbye);
    QByteArray buildResponse(bool goodbye) const;
    void sendAll(const QByteArray &packet);
    void handleRecords(const QList<Rr> &records);
    static QList<Rr> parsePacket(const QByteArray &p, bool *isResponse, QList<QPair<QStringList, quint16>> *questions);
    QStringList hostLabels() const;

    QThread m_thread;
    QUdpSocket *m_sock = nullptr;
    QTimer *m_queryTimer = nullptr, *m_announceTimer = nullptr;
    QList<QHostAddress> m_ips;
    // Anuncio
    bool m_publishing = false;
    QString m_instance;
    int m_port = 0;
    QMap<QString, QString> m_txt;
    qint64 m_lastResponse = 0;
    int m_announceLeft = 0;
    // Búsqueda
    bool m_browsing = false;
    QHash<QString, Instance> m_found;       // por nombre de instancia
    QHash<QString, QStringList> m_addrs;    // host → IPv4
    mutable QMutex m_mutex;
    bool m_announcingFlag = false;
};
