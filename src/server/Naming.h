#pragma once
#include <QString>
#include <optional>

/**
 * Interpretación de nombres de archivo y carpeta (título, año, temporada, episodio). Port literal del parser de Flutter/Node:
 * los resultados deben ser IDÉNTICOS para que los datos creados por una versión sigan siendo válidos en otra.
 */
namespace Naming {

struct ParsedTitle {
    QString title;
    std::optional<int> year;
};

struct SeasonEpisode {
    std::optional<int> season;   // ausente cuando el patrón no trae temporada («Episodio 03»)
    int episode = 0;
};

/** Última extensión (con el punto) como `path.extname` de Node; vacía si no hay. */
QString extName(const QString &name);
QString baseName(const QString &path);
QString nameWithoutExtension(const QString &path);

/** Título y año a partir de una ruta relativa; se prefiere la carpeta si no es genérica («CD1», «Season 1»…). */
ParsedTitle parseTitle(const QString &relativePath);

/** Número de temporada si `name` es una carpeta de temporada reconocida. */
std::optional<int> seasonOfFolder(const QString &name);
/** Temporada y episodio explícitos: SxxExx, NxNN, «Episodio NN». */
std::optional<SeasonEpisode> explicitEpisode(const QString &fileName);

/** Orden «natural» (Episodio 2 antes que Episodio 10); <0, 0 o >0. */
int compareNatural(const QString &a, const QString &b);

}
