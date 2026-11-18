#include "Naming.h"

#include <QRegularExpression>
#include <QStringList>

namespace Naming {

namespace {

const QRegularExpression &reYear() { static const QRegularExpression r(QStringLiteral(R"((?:19|20)\d{2})")); return r; }
const QRegularExpression &reNoise()
{
    static const QRegularExpression r(
        QStringLiteral(R"(\b(2160p|1080p|720p|480p|4k|uhd|hdr|bluray|blu-ray|bdrip|brrip|dvdrip|webrip|web-dl|webdl|hdtv|hdrip|remux|x264|x265|h264|h265|hevc|aac|ac3|dts|5\.1|7\.1|proper|extended|unrated|dual|latino|castellano|subtitulado)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    return r;
}
const QRegularExpression &reSeasonFolder() { static const QRegularExpression r(QStringLiteral(R"(^(?:season|temporada)\s*0*(\d{1,2})$)"), QRegularExpression::CaseInsensitiveOption); return r; }
const QRegularExpression &reSxxExx() { static const QRegularExpression r(QStringLiteral(R"([Ss](\d{1,2})[._ -]?[Ee](\d{1,3}))")); return r; }
const QRegularExpression &reNxNN() { static const QRegularExpression r(QStringLiteral(R"((?<!\d)(\d{1,2})x(\d{1,3})(?!\d))"), QRegularExpression::CaseInsensitiveOption); return r; }
const QRegularExpression &reEpisodeWord()
{
    static const QRegularExpression r(QStringLiteral(R"(\b(?:episodio|episode|cap[ií]tulo|capitulo|cap|ep)\b\.?\s*0*(\d{1,3}))"), QRegularExpression::CaseInsensitiveOption);
    return r;
}

bool isGeneric(const QString &name)
{
    const QString n = name.trimmed().toLower();
    if (n.isEmpty()) return true;
    static const QRegularExpression digits(QStringLiteral(R"(^\d+$)"));
    static const QRegularExpression words(QStringLiteral(R"(^(cd|disc|disco|part|parte|season|temporada)\s*\d*$)"));
    return digits.match(n).hasMatch() || words.match(n).hasMatch();
}

ParsedTitle parseText(const QString &text)
{
    QString clean = text;
    clean.replace(QRegularExpression(QStringLiteral(R"([._])")), QStringLiteral(" "));
    clean.replace(QRegularExpression(QStringLiteral(R"([\[\]{}])")), QStringLiteral(" "));

    // Se toma la ÚLTIMA coincidencia de año: en release names el año va después del título, así que un número que es
    // parte del título (la película «1917») no se confunde con el año.
    QRegularExpressionMatch last;
    auto it = reYear().globalMatch(clean);
    while (it.hasNext()) last = it.next();
    ParsedTitle out;
    QString title;
    if (last.hasMatch()) {
        out.year = last.captured(0).toInt();
        title = clean.left(last.capturedStart());
    } else {
        const QRegularExpressionMatch noise = reNoise().match(clean);
        title = noise.hasMatch() ? clean.left(noise.capturedStart()) : clean;
    }
    title.remove(QRegularExpression(QStringLiteral(R"([-–(]\s*$)")));
    title.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));
    title = title.trimmed();
    out.title = title.isEmpty() ? text.trimmed() : title;
    return out;
}

QStringList chunks(const QString &t)
{
    QStringList out;
    static const QRegularExpression re(QStringLiteral(R"(\d+|\D+)"));
    auto it = re.globalMatch(t);
    while (it.hasNext()) out << it.next().captured(0);
    return out;
}

} // namespace

QString extName(const QString &name)
{
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    return dot <= 0 ? QString() : name.mid(dot);
}

QString baseName(const QString &path)
{
    QString p = path;
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (p.size() > 1 && p.endsWith(QLatin1Char('/'))) p.chop(1);
    const int slash = p.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? p : p.mid(slash + 1);
}

QString nameWithoutExtension(const QString &path)
{
    const QString b = baseName(path);
    const QString e = extName(b);
    return e.isEmpty() ? b : b.left(b.size() - e.size());
}

ParsedTitle parseTitle(const QString &relativePath)
{
    QString p = relativePath;
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QStringList parts = p.split(QLatin1Char('/'));
    const QString file = nameWithoutExtension(parts.last());
    const bool hasFolder = parts.size() > 1;
    const QString folder = hasFolder ? parts.at(parts.size() - 2) : QString();
    return parseText(hasFolder && !isGeneric(folder) ? folder : file);
}

std::optional<int> seasonOfFolder(const QString &name)
{
    const auto m = reSeasonFolder().match(name.trimmed());
    return m.hasMatch() ? std::optional<int>(m.captured(1).toInt()) : std::nullopt;
}

std::optional<SeasonEpisode> explicitEpisode(const QString &fileName)
{
    auto m = reSxxExx().match(fileName);
    if (m.hasMatch()) return SeasonEpisode{m.captured(1).toInt(), m.captured(2).toInt()};
    m = reNxNN().match(fileName);
    if (m.hasMatch()) return SeasonEpisode{m.captured(1).toInt(), m.captured(2).toInt()};
    m = reEpisodeWord().match(fileName);
    if (m.hasMatch()) return SeasonEpisode{std::nullopt, m.captured(1).toInt()};
    return std::nullopt;
}

int compareNatural(const QString &a, const QString &b)
{
    const QStringList ta = chunks(a), tb = chunks(b);
    const int n = qMin(ta.size(), tb.size());
    static const QRegularExpression digits(QStringLiteral(R"(^\d+$)"));
    for (int i = 0; i < n; ++i) {
        const bool da = digits.match(ta.at(i)).hasMatch(), db = digits.match(tb.at(i)).hasMatch();
        int cmp;
        if (da && db) {
            const double x = ta.at(i).toDouble(), y = tb.at(i).toDouble();
            cmp = x < y ? -1 : (x > y ? 1 : 0);
        } else {
            const QString x = ta.at(i).toLower(), y = tb.at(i).toLower();
            cmp = x < y ? -1 : (x > y ? 1 : 0);
        }
        if (cmp != 0) return cmp;
    }
    return int(ta.size()) - int(tb.size());
}

} // namespace Naming
