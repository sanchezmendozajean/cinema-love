#include "Mdns.h"

#include <QDateTime>
#include <QMutexLocker>
#include <QNetworkInterface>
#include <QSysInfo>

namespace {

const QStringList kService = {QStringLiteral("_cinemalove"), QStringLiteral("_tcp"), QStringLiteral("local")};
constexpr quint16 kPtr = 12, kSrv = 33, kTxt = 16, kA = 1, kAny = 255;
const QHostAddress kGroup(QStringLiteral("224.0.0.251"));
constexpr quint16 kPort = 5353;

void put16(QByteArray &b, quint16 v) { b.append(char(v >> 8)); b.append(char(v & 0xFF)); }
void put32(QByteArray &b, quint32 v) { put16(b, quint16(v >> 16)); put16(b, quint16(v & 0xFFFF)); }

void putName(QByteArray &b, const QStringList &labels)
{
    for (const QString &l : labels) {
        const QByteArray u = l.toUtf8().left(63);
        b.append(char(u.size()));
        b.append(u);
    }
    b.append(char(0));
}

void putRecord(QByteArray &b, const QStringList &name, quint16 type, bool flush, quint32 ttl, const QByteArray &rdata)
{
    putName(b, name);
    put16(b, type);
    put16(b, quint16(flush ? 0x8001 : 0x0001));
    put32(b, ttl);
    put16(b, quint16(rdata.size()));
    b.append(rdata);
}

quint16 get16(const QByteArray &p, int pos) { return quint16((quint8(p[pos]) << 8) | quint8(p[pos + 1])); }

/** Lee un nombre (con compresión). Devuelve false si el paquete está mal formado. */
bool readName(const QByteArray &p, int &pos, QStringList *out)
{
    int cur = pos, jumps = 0;
    bool jumped = false;
    out->clear();
    while (true) {
        if (cur >= p.size()) return false;
        const int len = quint8(p[cur]);
        if (len == 0) { if (!jumped) pos = cur + 1; return true; }
        if ((len & 0xC0) == 0xC0) {
            if (cur + 1 >= p.size() || ++jumps > 20) return false;
            const int ptr = ((len & 0x3F) << 8) | quint8(p[cur + 1]);
            if (!jumped) pos = cur + 2;
            jumped = true;
            cur = ptr;
            continue;
        }
        if (len > 63 || cur + 1 + len > p.size()) return false;
        out->append(QString::fromUtf8(p.constData() + cur + 1, len));
        cur += 1 + len;
    }
}

bool sameName(const QStringList &a, const QStringList &b)
{
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i) if (a[i].compare(b[i], Qt::CaseInsensitive) != 0) return false;
    return true;
}

QString sanitizeHost(const QString &s)
{
    QString out;
    for (const QChar c : s) out += (c.isLetterOrNumber() && c.unicode() < 128) || c == QLatin1Char('-') ? c : QLatin1Char('-');
    while (out.startsWith(QLatin1Char('-'))) out.remove(0, 1);
    return out.isEmpty() ? QStringLiteral("cinemalove") : out.left(60);
}

QList<QNetworkInterface> lanInterfaces()
{
    QList<QNetworkInterface> r;
    for (const QNetworkInterface &i : QNetworkInterface::allInterfaces()) {
        if (!(i.flags() & QNetworkInterface::IsUp) || !(i.flags() & QNetworkInterface::IsRunning) || (i.flags() & QNetworkInterface::IsLoopBack)) continue;
        if (!(i.flags() & QNetworkInterface::CanMulticast)) continue;
        bool v4 = false;
        for (const QNetworkAddressEntry &e : i.addressEntries()) if (e.ip().protocol() == QAbstractSocket::IPv4Protocol) v4 = true;
        if (v4) r << i;
    }
    return r;
}

} // namespace

Mdns::Mdns()
{
    moveToThread(&m_thread);
    m_thread.start();
}

Mdns::~Mdns()
{
    QMetaObject::invokeMethod(this, [this] {
        if (m_publishing) { sendAnnouncement(true); m_publishing = false; }
        m_browsing = false;
        delete m_queryTimer; m_queryTimer = nullptr;
        delete m_announceTimer; m_announceTimer = nullptr;
        delete m_sock; m_sock = nullptr;
    }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait(2000);
}

void Mdns::ensureSocket()
{
    if (m_sock) return;
    m_sock = new QUdpSocket(this);
    if (!m_sock->bind(QHostAddress::AnyIPv4, kPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        delete m_sock;
        m_sock = nullptr;
        return;
    }
    m_sock->setSocketOption(QAbstractSocket::MulticastTtlOption, 255);
    m_sock->setSocketOption(QAbstractSocket::MulticastLoopbackOption, 1);
    const auto ifaces = lanInterfaces();
    if (ifaces.isEmpty()) m_sock->joinMulticastGroup(kGroup);
    for (const QNetworkInterface &i : ifaces) m_sock->joinMulticastGroup(kGroup, i);
    connect(m_sock, &QUdpSocket::readyRead, this, &Mdns::onReadyRead);
    m_queryTimer = new QTimer(this);
    connect(m_queryTimer, &QTimer::timeout, this, [this] { if (m_browsing) sendQuery(); });
    m_announceTimer = new QTimer(this);
    m_announceTimer->setSingleShot(true);
    connect(m_announceTimer, &QTimer::timeout, this, [this] {
        if (!m_publishing || m_announceLeft <= 0) return;
        --m_announceLeft;
        sendAnnouncement(false);
        if (m_announceLeft > 0) m_announceTimer->start(1000);
    });
}

void Mdns::closeSocketIfIdle()
{
    if (m_publishing || m_browsing || !m_sock) return;
    delete m_queryTimer; m_queryTimer = nullptr;
    delete m_announceTimer; m_announceTimer = nullptr;
    delete m_sock; m_sock = nullptr;
}

QStringList Mdns::hostLabels() const { return {sanitizeHost(QSysInfo::machineHostName()), QStringLiteral("local")}; }

void Mdns::publish(const QString &instanceName, int port, const QMap<QString, QString> &txt)
{
    QMetaObject::invokeMethod(this, [=] {
        ensureSocket();
        if (!m_sock) return;
        m_instance = instanceName;
        m_port = port;
        m_txt = txt;
        m_publishing = true;
        { QMutexLocker l(&m_mutex); m_announcingFlag = true; }
        m_announceLeft = 3;
        m_announceTimer->start(0);
    }, Qt::QueuedConnection);
}

void Mdns::unpublish()
{
    QMetaObject::invokeMethod(this, [this] {
        if (m_publishing && m_sock) sendAnnouncement(true);
        m_publishing = false;
        { QMutexLocker l(&m_mutex); m_announcingFlag = false; }
        if (m_announceTimer) m_announceTimer->stop();
        closeSocketIfIdle();
    }, Qt::BlockingQueuedConnection);
}

bool Mdns::announcing() const { QMutexLocker l(&m_mutex); return m_announcingFlag; }

void Mdns::startBrowse()
{
    QMetaObject::invokeMethod(this, [this] {
        ensureSocket();
        if (!m_sock) return;
        m_browsing = true;
        sendQuery();
        m_queryTimer->start(4000);
    }, Qt::QueuedConnection);
}

void Mdns::stopBrowse()
{
    QMetaObject::invokeMethod(this, [this] {
        m_browsing = false;
        if (m_queryTimer) m_queryTimer->stop();
        { QMutexLocker l(&m_mutex); m_found.clear(); m_addrs.clear(); }
        closeSocketIfIdle();
    }, Qt::QueuedConnection);
}

QList<Mdns::Found> Mdns::servers() const
{
    QList<Found> out;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QMutexLocker l(&m_mutex);
    for (auto it = m_found.constBegin(); it != m_found.constEnd(); ++it) {
        const Instance &i = it.value();
        const QString id = i.txt.value(QStringLiteral("id"));
        if (id.isEmpty() || i.port <= 0 || i.host.isEmpty() || now - i.seen > 30000) continue;
        const QStringList addrs = m_addrs.value(i.host.toLower());
        Found f;
        f.id = id;
        f.name = i.txt.value(QStringLiteral("name"), it.key());
        f.host = addrs.isEmpty() ? i.host : addrs.first();
        f.port = i.port;
        f.tls = i.txt.value(QStringLiteral("tls")) == QLatin1String("1");
        out << f;
    }
    return out;
}

void Mdns::sendAll(const QByteArray &packet)
{
    if (!m_sock) return;
    const auto ifaces = lanInterfaces();
    if (ifaces.isEmpty()) { m_sock->writeDatagram(packet, kGroup, kPort); return; }
    for (const QNetworkInterface &i : ifaces) {
        m_sock->setMulticastInterface(i);
        m_sock->writeDatagram(packet, kGroup, kPort);
    }
}

void Mdns::sendQuery()
{
    QByteArray b;
    put16(b, 0); put16(b, 0); put16(b, 1); put16(b, 0); put16(b, 0); put16(b, 0);
    putName(b, kService);
    put16(b, kPtr);
    put16(b, 1);
    sendAll(b);
}

QByteArray Mdns::buildResponse(bool goodbye) const
{
    const QStringList instance = QStringList{m_instance} + kService;
    const QStringList host = hostLabels();
    QList<QHostAddress> ips;
    for (const QNetworkInterface &i : lanInterfaces())
        for (const QNetworkAddressEntry &e : i.addressEntries()) if (e.ip().protocol() == QAbstractSocket::IPv4Protocol) ips << e.ip();
    const quint32 ttlLong = goodbye ? 0 : 4500, ttlShort = goodbye ? 0 : 120;

    QByteArray ptr; putName(ptr, instance);
    QByteArray srv; put16(srv, 0); put16(srv, 0); put16(srv, quint16(m_port)); putName(srv, host);
    QByteArray txt;
    for (auto it = m_txt.constBegin(); it != m_txt.constEnd(); ++it) {
        const QByteArray kv = (it.key() + QLatin1Char('=') + it.value()).toUtf8().left(255);
        txt.append(char(kv.size()));
        txt.append(kv);
    }
    if (txt.isEmpty()) txt.append(char(0));

    QByteArray b;
    put16(b, 0); put16(b, 0x8400); put16(b, 0); put16(b, 1); put16(b, 0); put16(b, quint16(2 + ips.size()));
    putRecord(b, kService, kPtr, false, ttlLong, ptr);
    putRecord(b, instance, kSrv, true, ttlShort, srv);
    putRecord(b, instance, kTxt, true, ttlLong, txt);
    for (const QHostAddress &ip : ips) {
        QByteArray a;
        put32(a, ip.toIPv4Address());
        putRecord(b, host, kA, true, ttlShort, a);
    }
    return b;
}

void Mdns::sendAnnouncement(bool goodbye)
{
    if (!m_sock || m_instance.isEmpty()) return;
    sendAll(buildResponse(goodbye));
}

QList<Mdns::Rr> Mdns::parsePacket(const QByteArray &p, bool *isResponse, QList<QPair<QStringList, quint16>> *questions)
{
    QList<Rr> out;
    if (p.size() < 12) return out;
    *isResponse = (quint8(p[2]) & 0x80) != 0;
    const int qd = get16(p, 4), total = get16(p, 6) + get16(p, 8) + get16(p, 10);
    int pos = 12;
    for (int i = 0; i < qd; ++i) {
        QStringList n;
        if (!readName(p, pos, &n) || pos + 4 > p.size()) return out;
        questions->append({n, get16(p, pos)});
        pos += 4;
    }
    for (int i = 0; i < total; ++i) {
        Rr r;
        if (!readName(p, pos, &r.name) || pos + 10 > p.size()) return out;
        r.type = get16(p, pos);
        r.cls = quint16(get16(p, pos + 2) & 0x7FFF);
        r.ttl = (quint32(get16(p, pos + 4)) << 16) | get16(p, pos + 6);
        const int len = get16(p, pos + 8);
        pos += 10;
        if (pos + len > p.size()) return out;
        const int dataStart = pos;
        r.data = p.mid(pos, len);
        if (r.type == kPtr) { int q = dataStart; readName(p, q, &r.target); }
        else if (r.type == kSrv && len >= 6) { r.port = get16(p, dataStart + 4); int q = dataStart + 6; readName(p, q, &r.target); }
        pos += len;
        out.append(r);
    }
    return out;
}

void Mdns::onReadyRead()
{
    while (m_sock && m_sock->hasPendingDatagrams()) {
        QByteArray d;
        d.resize(int(m_sock->pendingDatagramSize()));
        m_sock->readDatagram(d.data(), d.size());
        bool resp = false;
        QList<QPair<QStringList, quint16>> questions;
        const QList<Rr> records = parsePacket(d, &resp, &questions);
        if (!resp) {
            if (!m_publishing) continue;
            const QStringList instance = QStringList{m_instance} + kService;
            bool mine = false;
            for (const auto &q : questions) {
                const bool svc = sameName(q.first, kService) && (q.second == kPtr || q.second == kAny);
                const bool inst = sameName(q.first, instance) && (q.second == kSrv || q.second == kTxt || q.second == kAny);
                const bool host = sameName(q.first, hostLabels()) && (q.second == kA || q.second == kAny);
                if (svc || inst || host) mine = true;
            }
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (mine && now - m_lastResponse > 250) { m_lastResponse = now; sendAnnouncement(false); }
        } else if (m_browsing) {
            handleRecords(records);
        }
    }
}

void Mdns::handleRecords(const QList<Rr> &records)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QMutexLocker l(&m_mutex);
    auto instanceOf = [](const QStringList &n) -> QString {
        if (n.size() != 4) return {};
        for (int i = 0; i < 3; ++i) if (n[i + 1].compare(kService[i], Qt::CaseInsensitive) != 0) return {};
        return n[0];
    };
    for (const Rr &r : records) {
        if (r.type == kPtr && sameName(r.name, kService)) {
            const QString inst = instanceOf(r.target);
            if (inst.isEmpty()) continue;
            if (r.ttl == 0) { m_found.remove(inst); continue; }
            Instance &i = m_found[inst];
            i.ptr = true;
            i.seen = now;
        } else if (r.type == kSrv) {
            const QString inst = instanceOf(r.name);
            if (inst.isEmpty()) continue;
            Instance &i = m_found[inst];
            i.host = r.target.join(QLatin1Char('.'));
            i.port = r.port;
            i.seen = now;
        } else if (r.type == kTxt) {
            const QString inst = instanceOf(r.name);
            if (inst.isEmpty()) continue;
            Instance &i = m_found[inst];
            i.txt.clear();
            int pos = 0;
            while (pos < r.data.size()) {
                const int len = quint8(r.data[pos]);
                const QString kv = QString::fromUtf8(r.data.mid(pos + 1, len));
                const int eq = kv.indexOf(QLatin1Char('='));
                if (eq > 0) i.txt.insert(kv.left(eq), kv.mid(eq + 1));
                pos += 1 + len;
            }
            i.seen = now;
        } else if (r.type == kA && r.data.size() == 4) {
            const QString addr = QHostAddress(quint32((quint8(r.data[0]) << 24) | (quint8(r.data[1]) << 16) | (quint8(r.data[2]) << 8) | quint8(r.data[3]))).toString();
            QStringList &list = m_addrs[r.name.join(QLatin1Char('.')).toLower()];
            if (!list.contains(addr)) list << addr;
        }
    }
}
