import { basename, extname } from 'node:path'

/*
 * Port literal de `filename_parser.dart` y `episode_parser.dart` (Flutter). Los resultados deben ser IDÉNTICOS a los de
 * la versión anterior para que los datos creados por una versión sigan siendo válidos en la otra (specs/06 §3 y §5):
 * no «mejorar» las expresiones regulares sin cambiar también la versión Flutter.
 */

export interface TituloParseado {
  titulo: string
  anio?: number
}

const ANIO = /(?:19|20)\d{2}/g

// Marcadores de calidad/origen/codec/grupo: desde aquí en adelante todo es ruido, no parte del título.
const RUIDO =
  /\b(2160p|1080p|720p|480p|4k|uhd|hdr|bluray|blu-ray|bdrip|brrip|dvdrip|webrip|web-dl|webdl|hdtv|hdrip|remux|x264|x265|h264|h265|hevc|aac|ac3|dts|5\.1|7\.1|proper|extended|unrated|dual|latino|castellano|subtitulado)\b/i

/** `p.basenameWithoutExtension` de Dart: quita solo la última extensión. */
export function nombreSinExtension(ruta: string): string {
  const b = basename(ruta)
  const e = extname(b)
  return e ? b.slice(0, -e.length) : b
}

function esGenerico(nombre: string): boolean {
  const n = nombre.trim().toLowerCase()
  if (n === '') return true
  if (/^\d+$/.test(n)) return true
  if (/^(cd|disc|disco|part|parte|season|temporada)\s*\d*$/.test(n)) return true
  return false
}

function parsearTexto(texto: string): TituloParseado {
  let limpio = texto.replace(/[._]/g, ' ')
  limpio = limpio.replace(/[[\]{}]/g, ' ')

  // Se toma la ÚLTIMA coincidencia de año: en release names el año va después del título, así que un número que es
  // parte del título (la película «1917») no se confunde con el año.
  const coincidencias = [...limpio.matchAll(ANIO)]
  const matchAnio = coincidencias.length ? coincidencias[coincidencias.length - 1] : null
  let anio: number | undefined
  let titulo: string

  if (matchAnio) {
    anio = Number.parseInt(matchAnio[0], 10)
    titulo = limpio.slice(0, matchAnio.index)
  } else {
    const ruido = RUIDO.exec(limpio)
    titulo = ruido ? limpio.slice(0, ruido.index) : limpio
  }

  titulo = titulo
    .replace(/[-–(]\s*$/, '')
    .replace(/\s+/g, ' ')
    .trim()

  return { titulo: titulo === '' ? texto.trim() : titulo, anio }
}

/**
 * Título y año a partir de una ruta relativa. Se prefiere el nombre de la carpeta cuando existe y no es genérico
 * («CD1», «Season 1», numérico…): suele venir más limpio que el del archivo, que trae el release completo.
 */
export function parsearTitulo(rutaRelativa: string): TituloParseado {
  const partes = rutaRelativa.replace(/\\/g, '/').split('/')
  const nombreArchivo = nombreSinExtension(partes[partes.length - 1])
  const nombreCarpeta = partes.length > 1 ? partes[partes.length - 2] : null
  const fuente = nombreCarpeta != null && !esGenerico(nombreCarpeta) ? nombreCarpeta : nombreArchivo
  return parsearTexto(fuente)
}

// --- episodios

export interface TemporadaEpisodio {
  /** Ausente cuando el patrón no trae temporada («Episodio 03», a diferencia de «S01E03»). */
  temporada?: number
  episodio: number
}

const CARPETA_TEMPORADA = /^(?:season|temporada)\s*0*(\d{1,2})$/i
// «S01E02», «S01.E02», «S01 - E02»: variantes comunes de release names sin ser tan laxo como para casar resoluciones.
const SXX_EXX = /[Ss](\d{1,2})[._ -]?[Ee](\d{1,3})/
// Los lookarounds evitan confundir «1x02» con el «x» de una resolución como «1920x1080».
const NXNN = /(?<!\d)(\d{1,2})x(\d{1,3})(?!\d)/i
const EPISODIO_PALABRA = /\b(?:episodio|episode|cap[ií]tulo|capitulo|cap|ep)\b\.?\s*0*(\d{1,3})/i

/** Número de temporada si `nombre` es una carpeta de temporada reconocida («Season 1», «Temporada01»…). */
export function numeroTemporadaDeCarpeta(nombre: string): number | null {
  const m = CARPETA_TEMPORADA.exec(nombre.trim())
  return m ? Number.parseInt(m[1], 10) : null
}

/** Temporada+episodio explícitos en un nombre de archivo, probados en este orden: SxxExx, NxNN, «Episodio NN». */
export function numeroEpisodioExplicito(nombreArchivo: string): TemporadaEpisodio | null {
  const sxx = SXX_EXX.exec(nombreArchivo)
  if (sxx) return { temporada: Number.parseInt(sxx[1], 10), episodio: Number.parseInt(sxx[2], 10) }
  const nx = NXNN.exec(nombreArchivo)
  if (nx) return { temporada: Number.parseInt(nx[1], 10), episodio: Number.parseInt(nx[2], 10) }
  const palabra = EPISODIO_PALABRA.exec(nombreArchivo)
  if (palabra) return { episodio: Number.parseInt(palabra[1], 10) }
  return null
}

const tramos = (t: string): string[] => t.match(/\d+|\D+/g) ?? []

/**
 * Orden «natural»: alterna tramos de dígitos (como número) y de texto (sin distinguir mayúsculas), así «Episodio 2»
 * va antes que «Episodio 10». Devuelve <0, 0 o >0 como el `compareTo` de Dart.
 */
export function compararNatural(a: string, b: string): number {
  const ta = tramos(a)
  const tb = tramos(b)
  const largo = Math.min(ta.length, tb.length)
  for (let i = 0; i < largo; i++) {
    const na = /^\d+$/.test(ta[i]) ? Number(ta[i]) : null
    const nb = /^\d+$/.test(tb[i]) ? Number(tb[i]) : null
    let cmp: number
    if (na != null && nb != null) cmp = na - nb
    else {
      const x = ta[i].toLowerCase()
      const y = tb[i].toLowerCase()
      cmp = x < y ? -1 : x > y ? 1 : 0
    }
    if (cmp !== 0) return cmp
  }
  return ta.length - tb.length
}
