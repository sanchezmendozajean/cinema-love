#include "Certs.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

// ---------------------------------------------------------------------------------------------------------------- números grandes
/** Entero sin signo de precisión arbitraria (little endian, palabras de 32 bits, sin ceros a la izquierda). Solo lo que RSA necesita. */
class Big
{
public:
    Big() = default;
    explicit Big(uint64_t v) { while (v) { d.push_back(uint32_t(v)); v >>= 32; } }

    static Big fromBytes(const QByteArray &b)
    {
        Big r;
        for (int i = int(b.size()) - 1, k = 0; i >= 0; --i, ++k) {
            if (k % 4 == 0) r.d.push_back(0);
            r.d.back() |= uint32_t(uint8_t(b[i])) << (8 * (k % 4));
        }
        r.trim();
        return r;
    }
    QByteArray toBytes() const
    {
        QByteArray out;
        for (size_t i = d.size(); i-- > 0;)
            for (int s = 24; s >= 0; s -= 8) out.append(char((d[i] >> s) & 0xFF));
        int z = 0;
        while (z < out.size() - 1 && out[z] == 0) ++z;
        return d.empty() ? QByteArray(1, 0) : out.mid(z);
    }
    bool isZero() const { return d.empty(); }
    bool isOdd() const { return !d.empty() && (d[0] & 1); }
    int bitLength() const
    {
        if (d.empty()) return 0;
        int bits = int(d.size() - 1) * 32, top = int(d.back());
        while (top) { ++bits; top = int(uint32_t(top) >> 1); }
        return bits;
    }
    bool bit(int i) const { return size_t(i / 32) < d.size() && ((d[size_t(i / 32)] >> (i % 32)) & 1); }
    void setBit(int i) { if (size_t(i / 32) >= d.size()) d.resize(size_t(i / 32) + 1, 0); d[size_t(i / 32)] |= 1u << (i % 32); }

    static int cmp(const Big &a, const Big &b)
    {
        if (a.d.size() != b.d.size()) return a.d.size() < b.d.size() ? -1 : 1;
        for (size_t i = a.d.size(); i-- > 0;) if (a.d[i] != b.d[i]) return a.d[i] < b.d[i] ? -1 : 1;
        return 0;
    }
    static Big add(const Big &a, const Big &b)
    {
        Big r;
        uint64_t c = 0;
        for (size_t i = 0; i < std::max(a.d.size(), b.d.size()) || c; ++i) {
            const uint64_t s = c + (i < a.d.size() ? a.d[i] : 0) + (i < b.d.size() ? b.d[i] : 0);
            r.d.push_back(uint32_t(s));
            c = s >> 32;
        }
        return r;
    }
    /** a - b, con a >= b. */
    static Big sub(const Big &a, const Big &b)
    {
        Big r;
        int64_t borrow = 0;
        for (size_t i = 0; i < a.d.size(); ++i) {
            int64_t t = int64_t(a.d[i]) - borrow - (i < b.d.size() ? int64_t(b.d[i]) : 0);
            borrow = t < 0 ? 1 : 0;
            if (t < 0) t += (int64_t(1) << 32);
            r.d.push_back(uint32_t(t));
        }
        r.trim();
        return r;
    }
    static Big mul(const Big &a, const Big &b)
    {
        if (a.isZero() || b.isZero()) return Big();
        Big r;
        r.d.assign(a.d.size() + b.d.size(), 0);
        for (size_t i = 0; i < a.d.size(); ++i) {
            uint64_t c = 0;
            for (size_t j = 0; j < b.d.size(); ++j) {
                const uint64_t t = uint64_t(a.d[i]) * b.d[j] + r.d[i + j] + c;
                r.d[i + j] = uint32_t(t);
                c = t >> 32;
            }
            r.d[i + b.d.size()] = uint32_t(c);
        }
        r.trim();
        return r;
    }
    /** Cociente y resto (algoritmo D de Knuth). */
    static void divmod(const Big &a, const Big &b, Big *q, Big *r)
    {
        if (cmp(a, b) < 0) { if (q) *q = Big(); if (r) *r = a; return; }
        if (b.d.size() == 1) {
            Big qq;
            qq.d.assign(a.d.size(), 0);
            uint64_t rem = 0;
            for (size_t i = a.d.size(); i-- > 0;) {
                const uint64_t cur = (rem << 32) | a.d[i];
                qq.d[i] = uint32_t(cur / b.d[0]);
                rem = cur % b.d[0];
            }
            qq.trim();
            if (q) *q = qq;
            if (r) *r = Big(rem);
            return;
        }
        const size_t n = b.d.size(), m = a.d.size() - n;
        int s = 0;
        while (!((b.d.back() << s) & 0x80000000u)) ++s;
        std::vector<uint32_t> vn(n), un(a.d.size() + 1);
        for (size_t i = n - 1; i > 0; --i) vn[i] = (b.d[i] << s) | (s ? uint32_t(uint64_t(b.d[i - 1]) >> (32 - s)) : 0);
        vn[0] = b.d[0] << s;
        un[a.d.size()] = s ? uint32_t(uint64_t(a.d.back()) >> (32 - s)) : 0;
        for (size_t i = a.d.size() - 1; i > 0; --i) un[i] = (a.d[i] << s) | (s ? uint32_t(uint64_t(a.d[i - 1]) >> (32 - s)) : 0);
        un[0] = a.d[0] << s;
        Big qq;
        qq.d.assign(m + 1, 0);
        for (size_t jj = m + 1; jj-- > 0;) {
            const size_t j = jj;
            const uint64_t num = (uint64_t(un[j + n]) << 32) | un[j + n - 1];
            uint64_t qhat = num / vn[n - 1], rhat = num % vn[n - 1];
            while (qhat >= (uint64_t(1) << 32) || qhat * vn[n - 2] > ((rhat << 32) | un[j + n - 2])) {
                --qhat;
                rhat += vn[n - 1];
                if (rhat >= (uint64_t(1) << 32)) break;
            }
            int64_t k = 0, t;
            for (size_t i = 0; i < n; ++i) {
                const uint64_t p = qhat * vn[i];
                t = int64_t(un[i + j]) - k - int64_t(p & 0xFFFFFFFFull);
                un[i + j] = uint32_t(t);
                k = int64_t(p >> 32) - (t >> 32);
            }
            t = int64_t(un[j + n]) - k;
            un[j + n] = uint32_t(t);
            qq.d[j] = uint32_t(qhat);
            if (t < 0) {
                --qq.d[j];
                uint64_t c = 0;
                for (size_t i = 0; i < n; ++i) {
                    const uint64_t t2 = uint64_t(un[i + j]) + vn[i] + c;
                    un[i + j] = uint32_t(t2);
                    c = t2 >> 32;
                }
                un[j + n] += uint32_t(c);
            }
        }
        qq.trim();
        if (q) *q = qq;
        if (r) {
            Big rr;
            rr.d.assign(n, 0);
            for (size_t i = 0; i < n; ++i) rr.d[i] = (un[i] >> s) | (s ? uint32_t(uint64_t(un[i + 1]) << (32 - s)) : 0);
            rr.trim();
            *r = rr;
        }
    }
    static Big mod(const Big &a, const Big &m) { Big r; divmod(a, m, nullptr, &r); return r; }
    static Big div(const Big &a, const Big &m) { Big q; divmod(a, m, &q, nullptr); return q; }
    static Big modexp(const Big &base, const Big &exp, const Big &m)
    {
        Big result(1), b = mod(base, m);
        for (int i = exp.bitLength() - 1; i >= 0; --i) {
            result = mod(mul(result, result), m);
            if (exp.bit(i)) result = mod(mul(result, b), m);
        }
        return result;
    }
    /** Inverso de `a` módulo `m` (0 si no existe). */
    static Big modinv(const Big &a, const Big &m)
    {
        Big r0 = m, r1 = mod(a, m), t0, t1(1);
        while (!r1.isZero()) {
            Big q, rem;
            divmod(r0, r1, &q, &rem);
            const Big qt = mod(mul(q, t1), m);
            const Big nt = cmp(t0, qt) >= 0 ? sub(t0, qt) : sub(add(t0, m), qt);
            r0 = r1; r1 = rem;
            t0 = t1; t1 = nt;
        }
        return cmp(r0, Big(1)) == 0 ? t0 : Big();
    }
    uint32_t modSmall(uint32_t m) const
    {
        uint64_t rem = 0;
        for (size_t i = d.size(); i-- > 0;) rem = ((rem << 32) | d[i]) % m;
        return uint32_t(rem);
    }

    std::vector<uint32_t> d;
    void trim() { while (!d.empty() && d.back() == 0) d.pop_back(); }
};

Big randomBig(int bits)
{
    QByteArray b((bits + 7) / 8, 0);
    for (int i = 0; i < b.size(); ++i) b[i] = char(QRandomGenerator::system()->bounded(256));
    return Big::fromBytes(b);
}

bool isProbablePrime(const Big &n)
{
    static const uint32_t smalls[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179, 181, 191, 193, 197, 199};
    for (uint32_t p : smalls) {
        if (Big::cmp(n, Big(p)) == 0) return true;
        if (n.modSmall(p) == 0) return false;
    }
    const Big one(1), nm1 = Big::sub(n, one);
    int s = 0;
    Big dd = nm1;
    while (!dd.isOdd()) {   // dd /= 2
        Big q;
        Big::divmod(dd, Big(2), &q, nullptr);
        dd = q;
        ++s;
    }
    static const uint32_t bases[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29};
    for (uint32_t a : bases) {
        Big x = Big::modexp(Big(a), dd, n);
        if (Big::cmp(x, one) == 0 || Big::cmp(x, nm1) == 0) continue;
        bool composite = true;
        for (int r = 1; r < s; ++r) {
            x = Big::mod(Big::mul(x, x), n);
            if (Big::cmp(x, nm1) == 0) { composite = false; break; }
            if (Big::cmp(x, one) == 0) break;
        }
        if (composite) return false;
    }
    return true;
}

Big generatePrime(int bits, const Big &e)
{
    while (true) {
        Big c = randomBig(bits);
        // Los dos bits altos a 1 (el producto de dos primos así tiene exactamente el doble de bits) y el bajo a 1 (impar).
        c.setBit(bits - 1);
        c.setBit(bits - 2);
        c.setBit(0);
        // Se descarta el exceso de bits por arriba (randomBig redondea a bytes).
        if (c.bitLength() != bits) continue;
        if (Big::mod(Big::sub(c, Big(1)), e).isZero()) continue;   // gcd(p-1, e) debe ser 1
        if (isProbablePrime(c)) return c;
    }
}

// ---------------------------------------------------------------------------------------------------------------------- ASN.1 DER
QByteArray derLen(int n)
{
    QByteArray b;
    if (n < 128) b.append(char(n));
    else if (n < 256) { b.append(char(0x81)); b.append(char(n)); }
    else { b.append(char(0x82)); b.append(char(n >> 8)); b.append(char(n & 0xFF)); }
    return b;
}
QByteArray der(char tag, const QByteArray &content) { return QByteArray(1, tag) + derLen(int(content.size())) + content; }
QByteArray derInt(const Big &v)
{
    QByteArray b = v.toBytes();
    if (uint8_t(b[0]) & 0x80) b.prepend(char(0));
    return der(0x02, b);
}
QByteArray derSmallInt(int v) { return der(0x02, QByteArray(1, char(v))); }
QByteArray derOid(const QByteArray &bytes) { return der(0x06, bytes); }
QByteArray derUtf8(const QString &s) { return der(0x0C, s.toUtf8()); }
QByteArray derSeq(const QByteArray &c) { return der(0x30, c); }
QByteArray derSet(const QByteArray &c) { return der(0x31, c); }
QByteArray derBitString(const QByteArray &c) { return der(0x03, QByteArray(1, char(0)) + c); }
QByteArray derUtcTime(const QDateTime &t) { return der(0x17, (t.toUTC().toString(QStringLiteral("yyMMddHHmmss")) + QLatin1Char('Z')).toLatin1()); }

QByteArray fromHex(const char *h) { return QByteArray::fromHex(h); }

QByteArray pem(const char *label, const QByteArray &derBytes)
{
    const QByteArray b64 = derBytes.toBase64();
    QByteArray out = QByteArray("-----BEGIN ") + label + "-----\n";
    for (int i = 0; i < b64.size(); i += 64) out += b64.mid(i, 64) + '\n';
    out += QByteArray("-----END ") + label + "-----\n";
    return out;
}

QByteArray unpem(const QByteArray &pemText)
{
    QByteArray b64;
    for (const QByteArray &line : pemText.split('\n')) {
        const QByteArray l = line.trimmed();
        if (l.isEmpty() || l.startsWith("-----")) continue;
        b64 += l;
    }
    return QByteArray::fromBase64(b64);
}

QByteArray algRsaSha256() { return derSeq(derOid(fromHex("2a864886f70d01010b")) + QByteArray::fromHex("0500")); }
QByteArray name(const QString &cn, const QString &org)
{
    auto rdn = [](const QByteArray &oid, const QString &v) { return derSet(derSeq(derOid(oid) + derUtf8(v))); };
    return derSeq(rdn(fromHex("550403"), cn) + rdn(fromHex("55040a"), org));
}

QByteArray extension(const QByteArray &oid, const QByteArray &value, bool critical = false)
{
    return derSeq(derOid(oid) + (critical ? QByteArray::fromHex("0101ff") : QByteArray()) + der(0x04, value));
}

} // namespace

namespace Certs {

Certificate generate(const QString &nameText, const QStringList &ips)
{
    const Big e(65537);
    Big p, q, n, phi, d;
    while (true) {
        p = generatePrime(1024, e);
        q = generatePrime(1024, e);
        if (Big::cmp(p, q) == 0) continue;
        n = Big::mul(p, q);
        if (n.bitLength() != 2048) continue;
        phi = Big::mul(Big::sub(p, Big(1)), Big::sub(q, Big(1)));
        d = Big::modinv(e, phi);
        if (!d.isZero()) break;
    }
    const Big dp = Big::mod(d, Big::sub(p, Big(1))), dq = Big::mod(d, Big::sub(q, Big(1))), qinv = Big::modinv(q, p);

    // Clave privada PKCS#1.
    const QByteArray keyDer = derSeq(derSmallInt(0) + derInt(n) + derInt(e) + derInt(d) + derInt(p) + derInt(q) + derInt(dp) + derInt(dq) + derInt(qinv));

    // Certificado X.509 v3.
    const QByteArray rsaPub = derSeq(derInt(n) + derInt(e));
    const QByteArray spki = derSeq(derSeq(derOid(fromHex("2a864886f70d010101")) + QByteArray::fromHex("0500")) + derBitString(rsaPub));
    QByteArray serial(16, 0);
    for (int i = 0; i < serial.size(); ++i) serial[i] = char(QRandomGenerator::system()->bounded(256));
    serial[0] = char((uint8_t(serial[0]) & 0x7F) | 0x01);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QByteArray validity = derSeq(derUtcTime(now.addDays(-1)) + derUtcTime(now.addYears(10)));

    static const QRegularExpression dnsOk(QStringLiteral("^[A-Za-z0-9.-]+$"));
    QByteArray san;
    QStringList dns = {QStringLiteral("localhost")};
    if (dnsOk.match(nameText).hasMatch() && !dns.contains(nameText)) dns << nameText;
    for (const QString &h : dns) san += der(char(0x82), h.toLatin1());
    QStringList addrs = {QStringLiteral("127.0.0.1")};
    for (const QString &ip : ips) if (!addrs.contains(ip)) addrs << ip;
    for (const QString &ip : addrs) {
        const QHostAddress a(ip);
        if (a.protocol() != QAbstractSocket::IPv4Protocol) continue;
        const quint32 v = a.toIPv4Address();
        QByteArray raw;
        for (int sh = 24; sh >= 0; sh -= 8) raw.append(char((v >> sh) & 0xFF));
        san += der(char(0x87), raw);
    }
    QByteArray exts;
    exts += extension(fromHex("551d13"), derSeq(QByteArray()), true);                                         // basicConstraints: CA = false
    exts += extension(fromHex("551d0f"), der(0x03, QByteArray::fromHex("05a0")), true);                       // keyUsage: digitalSignature + keyEncipherment
    exts += extension(fromHex("551d25"), derSeq(derOid(fromHex("2b06010505070301"))));                         // extKeyUsage: serverAuth
    exts += extension(fromHex("551d11"), derSeq(san));                                                         // subjectAltName
    const QByteArray issuer = name(QStringLiteral("CinemaLove"), QStringLiteral("CinemaLove"));
    const QByteArray tbs = derSeq(QByteArray::fromHex("a003020102") + derInt(Big::fromBytes(serial)) + algRsaSha256() + issuer + validity + issuer + spki + der(char(0xa3), derSeq(exts)));

    // Firma PKCS#1 v1.5 con SHA-256.
    const QByteArray digestInfo = fromHex("3031300d060960864801650304020105000420") + QCryptographicHash::hash(tbs, QCryptographicHash::Sha256);
    const int k = 256;
    QByteArray em;
    em.append(char(0x00)); em.append(char(0x01));
    em.append(QByteArray(k - 3 - int(digestInfo.size()), char(0xFF)));
    em.append(char(0x00));
    em.append(digestInfo);
    QByteArray sig = Big::modexp(Big::fromBytes(em), d, n).toBytes();
    if (sig.size() < k) sig.prepend(QByteArray(k - int(sig.size()), char(0)));

    const QByteArray certDer = derSeq(tbs + algRsaSha256() + derBitString(sig));
    Certificate c;
    c.certPem = pem("CERTIFICATE", certDer);
    c.keyPem = pem("RSA PRIVATE KEY", keyDer);
    c.fingerprint = fingerprintOf(c.certPem);
    return c;
}

QString fingerprintOf(const QByteArray &certPem)
{
    const QByteArray raw = unpem(certPem);
    if (raw.isEmpty()) return {};
    const QByteArray hex = QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex().toUpper();
    QStringList parts;
    for (int i = 0; i < hex.size(); i += 2) parts << QString::fromLatin1(hex.mid(i, 2));
    return parts.join(QLatin1Char(':'));
}

bool Storage::exists() const { return QFile::exists(m_dir + QStringLiteral("/server.crt")) && QFile::exists(m_dir + QStringLiteral("/server.key")); }

Certificate Storage::load() const
{
    Certificate c;
    QFile cf(m_dir + QStringLiteral("/server.crt")), kf(m_dir + QStringLiteral("/server.key"));
    if (!cf.open(QIODevice::ReadOnly) || !kf.open(QIODevice::ReadOnly)) return {};
    c.certPem = cf.readAll();
    c.keyPem = kf.readAll();
    c.fingerprint = fingerprintOf(c.certPem);
    return c.fingerprint.isEmpty() ? Certificate() : c;   // archivo corrupto: se regenerará
}

void Storage::save(const Certificate &c) const
{
    QDir().mkpath(m_dir);
    for (const auto &pr : {qMakePair(QStringLiteral("/server.crt"), c.certPem), qMakePair(QStringLiteral("/server.key"), c.keyPem)}) {
        QFile f(m_dir + pr.first);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { f.write(pr.second); f.close(); }
    }
    QFile::setPermissions(m_dir + QStringLiteral("/server.key"), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

Certificate Storage::loadOrGenerate(const QString &nameText, const QStringList &ips) const
{
    Certificate c = load();
    if (c.valid()) return c;
    c = generate(nameText, ips);
    save(c);
    return c;
}

Certificate Storage::regenerate(const QString &nameText, const QStringList &ips) const
{
    const Certificate c = generate(nameText, ips);
    save(c);
    return c;
}

}
