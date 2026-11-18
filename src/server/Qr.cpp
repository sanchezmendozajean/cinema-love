#include "Qr.h"

#include <QByteArray>
#include <algorithm>
#include <cstdlib>

namespace {

// Nivel de corrección M. Índice = versión (1-40).
const int kEccPerBlock[41] = {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28};
const int kNumBlocks[41] = {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49};

int rawModules(int ver)
{
    int r = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        const int na = ver / 7 + 2;
        r -= (25 * na - 10) * na - 55;
        if (ver >= 7) r -= 36;
    }
    return r;
}

int dataCodewords(int ver) { return rawModules(ver) / 8 - kEccPerBlock[ver] * kNumBlocks[ver]; }

int gfMul(int x, int y)
{
    int z = 0;
    for (int i = 7; i >= 0; --i) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return z;
}

QVector<int> rsDivisor(int degree)
{
    QVector<int> r(degree, 0);
    r[degree - 1] = 1;
    int root = 1;
    for (int i = 0; i < degree; ++i) {
        for (int j = 0; j < degree; ++j) {
            r[j] = gfMul(r[j], root);
            if (j + 1 < degree) r[j] ^= r[j + 1];
        }
        root = gfMul(root, 0x02);
    }
    return r;
}

QVector<int> rsRemainder(const QVector<int> &data, const QVector<int> &divisor)
{
    QVector<int> r(divisor.size(), 0);
    for (int b : data) {
        const int factor = b ^ r.first();
        r.remove(0);
        r.append(0);
        for (int i = 0; i < divisor.size(); ++i) r[i] ^= gfMul(divisor[i], factor);
    }
    return r;
}

struct Matrix {
    int size = 0;
    QVector<QVector<bool>> m, fn;
    explicit Matrix(int s) : size(s), m(s, QVector<bool>(s, false)), fn(s, QVector<bool>(s, false)) {}
    void setFn(int x, int y, bool v) { m[y][x] = v; fn[y][x] = true; }
    bool in(int x, int y) const { return x >= 0 && x < size && y >= 0 && y < size; }
};

void drawFinder(Matrix &q, int cx, int cy)
{
    for (int dy = -4; dy <= 4; ++dy)
        for (int dx = -4; dx <= 4; ++dx) {
            const int dist = std::max(std::abs(dx), std::abs(dy)), x = cx + dx, y = cy + dy;
            if (q.in(x, y)) q.setFn(x, y, dist != 2 && dist != 4);
        }
}

void drawAlignment(Matrix &q, int cx, int cy)
{
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) q.setFn(cx + dx, cy + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
}

QVector<int> alignPositions(int ver, int size)
{
    if (ver == 1) return {};
    const int na = ver / 7 + 2;
    const int step = (ver * 8 + na * 3 + 5) / (na * 4 - 4) * 2;
    QVector<int> r(na);
    r[0] = 6;
    for (int i = na - 1, pos = size - 7; i >= 1; --i, pos -= step) r[i] = pos;
    return r;
}

void drawFormat(Matrix &q, int mask)
{
    const int data = mask;   // nivel M = 0b00
    int rem = data;
    for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    const int bits = ((data << 10) | rem) ^ 0x5412;
    auto bit = [bits](int i) { return ((bits >> i) & 1) != 0; };
    for (int i = 0; i <= 5; ++i) q.setFn(8, i, bit(i));
    q.setFn(8, 7, bit(6));
    q.setFn(8, 8, bit(7));
    q.setFn(7, 8, bit(8));
    for (int i = 9; i < 15; ++i) q.setFn(14 - i, 8, bit(i));
    for (int i = 0; i < 8; ++i) q.setFn(q.size - 1 - i, 8, bit(i));
    for (int i = 8; i < 15; ++i) q.setFn(8, q.size - 15 + i, bit(i));
    q.setFn(8, q.size - 8, true);
}

void drawVersion(Matrix &q, int ver)
{
    if (ver < 7) return;
    int rem = ver;
    for (int i = 0; i < 12; ++i) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
    const int bits = (ver << 12) | rem;
    for (int i = 0; i < 18; ++i) {
        const bool c = ((bits >> i) & 1) != 0;
        const int a = q.size - 11 + i % 3, b = i / 3;
        q.setFn(a, b, c);
        q.setFn(b, a, c);
    }
}

void drawCodewords(Matrix &q, const QVector<int> &data)
{
    int i = 0;
    for (int right = q.size - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int vert = 0; vert < q.size; ++vert)
            for (int j = 0; j < 2; ++j) {
                const int x = right - j;
                const bool upward = ((right + 1) & 2) == 0;
                const int y = upward ? q.size - 1 - vert : vert;
                if (!q.fn[y][x] && i < data.size() * 8) {
                    q.m[y][x] = ((data[i >> 3] >> (7 - (i & 7))) & 1) != 0;
                    ++i;
                }
            }
    }
}

void applyMask(Matrix &q, int mask)
{
    for (int y = 0; y < q.size; ++y)
        for (int x = 0; x < q.size; ++x) {
            bool inv = false;
            switch (mask) {
            case 0: inv = (x + y) % 2 == 0; break;
            case 1: inv = y % 2 == 0; break;
            case 2: inv = x % 3 == 0; break;
            case 3: inv = (x + y) % 3 == 0; break;
            case 4: inv = (x / 3 + y / 2) % 2 == 0; break;
            case 5: inv = x * y % 2 + x * y % 3 == 0; break;
            case 6: inv = (x * y % 2 + x * y % 3) % 2 == 0; break;
            default: inv = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
            }
            if (!q.fn[y][x] && inv) q.m[y][x] = !q.m[y][x];
        }
}

int penalty(const Matrix &q)
{
    const int n = q.size;
    int result = 0;
    // N1: rachas de 5 o más módulos iguales (filas y columnas).
    for (int pass = 0; pass < 2; ++pass)
        for (int a = 0; a < n; ++a) {
            int run = 1;
            for (int b = 1; b < n; ++b) {
                const bool cur = pass == 0 ? q.m[a][b] : q.m[b][a], prev = pass == 0 ? q.m[a][b - 1] : q.m[b - 1][a];
                if (cur == prev) {
                    ++run;
                    if (run == 5) result += 3;
                    else if (run > 5) ++result;
                } else run = 1;
            }
        }
    // N2: bloques 2x2.
    for (int y = 0; y < n - 1; ++y)
        for (int x = 0; x < n - 1; ++x)
            if (q.m[y][x] == q.m[y][x + 1] && q.m[y][x] == q.m[y + 1][x] && q.m[y][x] == q.m[y + 1][x + 1]) result += 3;
    // N3: patrón parecido al buscador (1:1:3:1:1) con cuatro módulos claros a un lado.
    static const bool pat[11] = {true, false, true, true, true, false, true, false, false, false, false};
    static const bool rpat[11] = {false, false, false, false, true, false, true, true, true, false, true};
    for (int pass = 0; pass < 2; ++pass)
        for (int a = 0; a < n; ++a)
            for (int b = 0; b + 11 <= n; ++b) {
                bool m1 = true, m2 = true;
                for (int k = 0; k < 11 && (m1 || m2); ++k) {
                    const bool v = pass == 0 ? q.m[a][b + k] : q.m[b + k][a];
                    if (v != pat[k]) m1 = false;
                    if (v != rpat[k]) m2 = false;
                }
                if (m1 || m2) result += 40;
            }
    // N4: proporción de módulos oscuros.
    int dark = 0;
    for (const auto &row : q.m) for (bool v : row) dark += v;
    const int total = n * n;
    const int k = (std::abs(dark * 20 - total * 10) + total - 1) / total - 1;
    result += std::max(0, k) * 10;
    return result;
}

} // namespace

namespace Qr {

QVector<QVector<bool>> encode(const QString &text)
{
    const QByteArray bytes = text.toUtf8();
    int ver = 0;
    for (int v = 1; v <= 40; ++v) {
        const int countBits = v <= 9 ? 8 : 16;
        if (4 + countBits + int(bytes.size()) * 8 <= dataCodewords(v) * 8) { ver = v; break; }
    }
    if (!ver) return {};

    // Flujo de bits: modo byte + longitud + datos + terminador + relleno.
    QVector<bool> bits;
    auto put = [&bits](quint32 val, int len) { for (int i = len - 1; i >= 0; --i) bits.append(((val >> i) & 1) != 0); };
    put(0x4, 4);
    put(quint32(bytes.size()), ver <= 9 ? 8 : 16);
    for (char c : bytes) put(quint8(c), 8);
    const int capBits = dataCodewords(ver) * 8;
    put(0, std::min(4, capBits - int(bits.size())));
    put(0, (8 - int(bits.size()) % 8) % 8);
    QVector<int> data(bits.size() / 8, 0);
    for (int i = 0; i < bits.size(); ++i) data[i >> 3] |= (bits[i] ? 1 : 0) << (7 - (i & 7));
    for (int pad = 0xEC; data.size() < dataCodewords(ver); pad ^= 0xEC ^ 0x11) data.append(pad);

    // Bloques con corrección de errores, entrelazados.
    const int numBlocks = kNumBlocks[ver], eccLen = kEccPerBlock[ver], raw = rawModules(ver) / 8;
    const int numShort = numBlocks - raw % numBlocks, shortLen = raw / numBlocks;
    QVector<QVector<int>> blocks;
    const QVector<int> div = rsDivisor(eccLen);
    for (int i = 0, k = 0; i < numBlocks; ++i) {
        const int dl = shortLen - eccLen + (i < numShort ? 0 : 1);
        QVector<int> dat = data.mid(k, dl);
        k += dl;
        QVector<int> ecc = rsRemainder(dat, div);
        if (i < numShort) dat.append(0);   // marcador de hueco para igualar longitudes
        blocks.append(dat + ecc);
    }
    QVector<int> all;
    for (int i = 0; i < blocks.first().size(); ++i)
        for (int j = 0; j < numBlocks; ++j)
            if (i != shortLen - eccLen || j >= numShort) all.append(blocks[j][i]);

    // Patrones de función.
    const int size = ver * 4 + 17;
    Matrix q(size);
    for (int i = 0; i < size; ++i) { q.setFn(6, i, i % 2 == 0); q.setFn(i, 6, i % 2 == 0); }
    drawFinder(q, 3, 3);
    drawFinder(q, size - 4, 3);
    drawFinder(q, 3, size - 4);
    const QVector<int> ap = alignPositions(ver, size);
    for (int i = 0; i < ap.size(); ++i)
        for (int j = 0; j < ap.size(); ++j)
            if (!((i == 0 && j == 0) || (i == 0 && j == ap.size() - 1) || (i == ap.size() - 1 && j == 0))) drawAlignment(q, ap[i], ap[j]);
    drawFormat(q, 0);
    drawVersion(q, ver);
    drawCodewords(q, all);

    // Máscara con menor penalización.
    int best = 0, bestPen = 1 << 30;
    for (int mask = 0; mask < 8; ++mask) {
        applyMask(q, mask);
        drawFormat(q, mask);
        const int p = penalty(q);
        if (p < bestPen) { bestPen = p; best = mask; }
        applyMask(q, mask);   // deshacer
    }
    applyMask(q, best);
    drawFormat(q, best);
    return q.m;
}

QString svg(const QString &text, int border)
{
    const auto m = encode(text);
    if (m.isEmpty()) return {};
    const int n = int(m.size()), total = n + border * 2;
    QString path;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            if (!m[y][x]) continue;
            int run = 1;
            while (x + run < n && m[y][x + run]) ++run;
            path += QStringLiteral("M%1 %2h%3v1h-%3z").arg(x + border).arg(y + border).arg(run);
            x += run - 1;
        }
    return QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %1 %1\" shape-rendering=\"crispEdges\"><path fill=\"#ffffff\" d=\"M0 0h%1v%1H0z\"/><path fill=\"#000000\" d=\"%2\"/></svg>").arg(total).arg(path);
}

}
