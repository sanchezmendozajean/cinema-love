#include "Bcrypt.h"

#include <QByteArray>
#include <QRandomGenerator>
#include <cstring>

#include "BlowfishInit.inc"

namespace {

typedef unsigned int u32;

struct Blowfish {
    u32 P[18];
    u32 S[4][256];

    void init()
    {
        std::memcpy(P, kBlowfishInit, sizeof(P));
        std::memcpy(S, kBlowfishInit + 18, sizeof(S));
    }
    u32 F(u32 x) const { return ((S[0][x >> 24] + S[1][(x >> 16) & 0xFF]) ^ S[2][(x >> 8) & 0xFF]) + S[3][x & 0xFF]; }
    void encipher(u32 &xl, u32 &xr) const
    {
        u32 l = xl, r = xr;
        l ^= P[0];
        for (int i = 1; i <= 16; i += 2) {
            r ^= F(l) ^ P[i];
            l ^= F(r) ^ P[i + 1];
        }
        r ^= P[17];
        xl = r;
        xr = l;
    }
};

u32 streamToWord(const QByteArray &data, int &pos)
{
    u32 w = 0;
    for (int i = 0; i < 4; ++i) {
        w = (w << 8) | u32(uchar(data[pos]));
        pos = (pos + 1) % int(data.size());
    }
    return w;
}

/** ExpandKey(state, salt, key); con `salt` vacío es la variante sin sal. */
void expandKey(Blowfish &bf, const QByteArray &salt, const QByteArray &key)
{
    int j = 0;
    for (int i = 0; i < 18; ++i) bf.P[i] ^= streamToWord(key, j);
    j = 0;
    u32 l = 0, r = 0;
    const bool useSalt = !salt.isEmpty();
    for (int i = 0; i < 18; i += 2) {
        if (useSalt) { l ^= streamToWord(salt, j); r ^= streamToWord(salt, j); }
        bf.encipher(l, r);
        bf.P[i] = l;
        bf.P[i + 1] = r;
    }
    for (int s = 0; s < 4; ++s)
        for (int k = 0; k < 256; k += 2) {
            if (useSalt) { l ^= streamToWord(salt, j); r ^= streamToWord(salt, j); }
            bf.encipher(l, r);
            bf.S[s][k] = l;
            bf.S[s][k + 1] = r;
        }
}

const char kAlphabet[] = "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

QByteArray b64Encode(const QByteArray &d, int len)
{
    QByteArray out;
    int off = 0;
    while (off < len) {
        uchar c1 = uchar(d[off++]);
        out += kAlphabet[c1 >> 2];
        c1 = (c1 & 0x03) << 4;
        if (off >= len) { out += kAlphabet[c1]; break; }
        uchar c2 = uchar(d[off++]);
        c1 |= c2 >> 4;
        out += kAlphabet[c1];
        c1 = (c2 & 0x0F) << 2;
        if (off >= len) { out += kAlphabet[c1]; break; }
        c2 = uchar(d[off++]);
        c1 |= c2 >> 6;
        out += kAlphabet[c1];
        out += kAlphabet[c2 & 0x3F];
    }
    return out;
}

int b64Index(char c)
{
    const char *p = std::strchr(kAlphabet, c);
    return (p && c) ? int(p - kAlphabet) : -1;
}

bool b64Decode(const QByteArray &s, int maxLen, QByteArray *out)
{
    out->clear();
    int off = 0;
    while (out->size() < maxLen && off < s.size() - 1) {
        const int c1 = b64Index(s[off++]), c2 = b64Index(s[off++]);
        if (c1 < 0 || c2 < 0) return false;
        out->append(char((c1 << 2) | ((c2 & 0x30) >> 4)));
        if (out->size() >= maxLen || off >= s.size()) break;
        const int c3 = b64Index(s[off++]);
        if (c3 < 0) return false;
        out->append(char(((c2 & 0x0F) << 4) | ((c3 & 0x3C) >> 2)));
        if (out->size() >= maxLen || off >= s.size()) break;
        const int c4 = b64Index(s[off++]);
        if (c4 < 0) return false;
        out->append(char(((c3 & 0x03) << 6) | c4));
    }
    return out->size() == maxLen;
}

/** Calcula el hash completo `$2b$CC$<sal22><hash31>` a partir de contraseña, coste y sal (16 bytes). */
QByteArray compute(const QByteArray &password, int cost, const QByteArray &salt, char minor)
{
    QByteArray key = password.left(72);
    if (key.size() < 72) key.append('\0');   // la clave incluye el NUL final (como bcrypt de OpenBSD)
    Blowfish bf;
    bf.init();
    expandKey(bf, salt, key);
    const quint64 rounds = quint64(1) << cost;
    for (quint64 i = 0; i < rounds; ++i) {
        expandKey(bf, QByteArray(), key);
        expandKey(bf, QByteArray(), salt);
    }
    u32 cdata[6];
    const char magic[] = "OrpheanBeholderScryDoubt";
    for (int i = 0; i < 6; ++i) {
        cdata[i] = (u32(uchar(magic[4 * i])) << 24) | (u32(uchar(magic[4 * i + 1])) << 16) | (u32(uchar(magic[4 * i + 2])) << 8) | u32(uchar(magic[4 * i + 3]));
    }
    for (int i = 0; i < 64; ++i)
        for (int j = 0; j < 6; j += 2) bf.encipher(cdata[j], cdata[j + 1]);
    QByteArray ct;
    for (int i = 0; i < 6; ++i) {
        ct.append(char((cdata[i] >> 24) & 0xFF));
        ct.append(char((cdata[i] >> 16) & 0xFF));
        ct.append(char((cdata[i] >> 8) & 0xFF));
        ct.append(char(cdata[i] & 0xFF));
    }
    QByteArray out = "$2";
    out += minor;
    out += '$';
    out += QByteArray::number(cost).rightJustified(2, '0');
    out += '$';
    out += b64Encode(salt, 16);
    out += b64Encode(ct, 23);
    return out;
}

} // namespace

namespace Bcrypt {

bool verify(const QString &password, const QString &hash)
{
    const QByteArray h = hash.toLatin1();
    if (h.size() != 60 || h[0] != '$' || h[1] != '2' || h[3] != '$' || h[6] != '$') return false;
    const char minor = h[2];
    if (minor != 'a' && minor != 'b' && minor != 'y' && minor != 'x') return false;
    bool ok = false;
    const int cost = h.mid(4, 2).toInt(&ok);
    if (!ok || cost < 4 || cost > 31) return false;
    QByteArray salt;
    if (!b64Decode(h.mid(7, 22), 16, &salt)) return false;
    const QByteArray calc = compute(password.toUtf8(), cost, salt, minor);
    // Comparación en tiempo constante
    if (calc.size() != h.size()) return false;
    int diff = 0;
    for (int i = 0; i < h.size(); ++i) diff |= calc[i] ^ h[i];
    return diff == 0;
}

QString hash(const QString &password, int cost)
{
    QByteArray salt(16, 0);
    for (int i = 0; i < 16; ++i) salt[i] = char(QRandomGenerator::system()->bounded(256));
    return QString::fromLatin1(compute(password.toUtf8(), cost, salt, 'b'));
}

} // namespace Bcrypt
