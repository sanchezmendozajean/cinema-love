import type { TituloParseado } from './nombres'

export interface ResultadoBusqueda {
  id: number
  titulo: string
  tituloOriginal?: string | null
  anio?: number | null
  posterUrl?: string | null
  sinopsis?: string | null
}

// Se quita todo lo que no sea [a-z0-9 ] (¡también los acentos y la ñ, sin transliterar!) y se colapsan espacios: así
// «Título - Subtítulo» (carpeta) y «Título: Subtítulo» (TMDb) comparan igual. Idéntico a `_normalizar` de Flutter.
const normalizar = (s: string): string =>
  s
    .toLowerCase()
    .trim()
    .replace(/[^a-z0-9 ]/g, '')
    .replace(/\s+/g, ' ')
    .trim()

/**
 * Coincidencia inequívoca entre los resultados de TMDb y el nombre parseado (specs/06 §5); `null` = revisión manual.
 * Port de `elegirMejorCandidato` (tmdb_match.dart):
 * - Con año en el nombre: solo si UN resultado tiene ese año (o, de varios, uno solo coincide por título). Si el año
 *   no aparece en ningún resultado NO se asigna (puede haber un homónimo en otro año).
 * - Sin año: solo si hay UNA única coincidencia exacta de título (localizado u original) entre todos los resultados.
 */
export function elegirMejorCandidato(resultados: ResultadoBusqueda[], parseado: TituloParseado): ResultadoBusqueda | null {
  if (resultados.length === 0) return null
  const buscado = normalizar(parseado.titulo)
  const coincide = (r: ResultadoBusqueda): boolean =>
    normalizar(r.titulo) === buscado || (r.tituloOriginal != null && normalizar(r.tituloOriginal) === buscado)

  if (parseado.anio != null) {
    const porAnio = resultados.filter((r) => r.anio === parseado.anio)
    if (porAnio.length === 1) return porAnio[0]
    if (porAnio.length > 0) {
      const exactos = porAnio.filter(coincide)
      return exactos.length === 1 ? exactos[0] : null
    }
    return null
  }
  const exactos = resultados.filter(coincide)
  return exactos.length === 1 ? exactos[0] : null
}
