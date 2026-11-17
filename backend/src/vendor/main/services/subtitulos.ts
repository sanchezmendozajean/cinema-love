import { existsSync, readdirSync, readFileSync } from 'node:fs'
import { basename, dirname, extname, join } from 'node:path'
import type { PistaSubtitulo } from '../../shared/playback-types'

const EXTENSIONES = new Set(['.srt', '.vtt', '.ass', '.ssa'])
const CODEC_POR_EXTENSION: Record<string, string> = { '.srt': 'subrip', '.vtt': 'webvtt', '.ass': 'ass', '.ssa': 'ssa' }

/** Códigos de idioma de 2 letras que aparecen en nombres de archivo → ISO 639-2 (como los que informa ffprobe). */
const IDIOMAS: Record<string, string> = {
  es: 'spa', en: 'eng', pt: 'por', fr: 'fre', de: 'ger', it: 'ita', ja: 'jpn', ko: 'kor', zh: 'chi', ru: 'rus', ca: 'cat', nl: 'dut', pl: 'pol', sv: 'swe', tr: 'tur', ar: 'ara', hi: 'hin'
}

export interface SubtituloExterno extends PistaSubtitulo {
  externo: true
  /** Ruta absoluta del archivo. */
  archivo: string
}

/**
 * Subtítulos externos junto al video con el mismo nombre base: `pelicula.srt`, `pelicula.es.srt`,
 * `pelicula.forced.srt`, `pelicula.es.forced.srt`. Orden alfabético (así los numera mpv) → `ext-1`, `ext-2`…
 */
export function buscarSubtitulosExternos(rutaVideo: string): SubtituloExterno[] {
  const dir = dirname(rutaVideo)
  const base = basename(rutaVideo, extname(rutaVideo))
  if (!existsSync(dir)) return []
  const archivos = readdirSync(dir)
    .filter((f) => EXTENSIONES.has(extname(f).toLowerCase()) && f.toLowerCase().startsWith(`${base.toLowerCase()}.`))
    .sort((a, b) => a.localeCompare(b))

  return archivos.map((f, i) => {
    // Lo que queda entre el nombre base y la extensión: «.es», «.forced», «.es.forced»…
    const medio = f.slice(base.length + 1, f.length - extname(f).length)
    const tokens = medio.split('.').filter(Boolean).map((t) => t.toLowerCase())
    const forzado = tokens.includes('forced') || tokens.includes('forzado') || tokens.includes('forzados')
    const cod = tokens.find((t) => IDIOMAS[t] ?? (t.length === 3 && /^[a-z]{3}$/.test(t) && !['sdh', 'cc'].includes(t)))
    return {
      id: `ext-${i + 1}`,
      indice: null,
      codec: CODEC_POR_EXTENSION[extname(f).toLowerCase()] ?? 'subrip',
      idioma: cod ? (IDIOMAS[cod] ?? cod) : null,
      titulo: f,
      forzado,
      tipo: 'texto' as const,
      externo: true as const,
      archivo: join(dir, f)
    }
  })
}

/** Lee un subtítulo de texto como UTF-8; si no es UTF-8 válido (muy común en SRT viejos) asume Windows-1252. */
export function leerTextoSubtitulo(archivo: string): string {
  let buf = readFileSync(archivo)
  if (buf[0] === 0xef && buf[1] === 0xbb && buf[2] === 0xbf) buf = buf.subarray(3)
  try {
    return new TextDecoder('utf-8', { fatal: true }).decode(buf)
  } catch {
    return new TextDecoder('windows-1252').decode(buf)
  }
}
