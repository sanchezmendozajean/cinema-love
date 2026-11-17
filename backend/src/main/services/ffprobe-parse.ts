import { extname } from 'node:path'
import type { PistaAudio, PistaSubtitulo, PistaVideo } from '../../shared/playback-types'

/** Salida cruda de `ffprobe -print_format json -show_format -show_streams` (solo lo que se usa). */
export interface FfprobeJson {
  format?: { format_name?: string; duration?: string; size?: string }
  streams?: {
    index: number
    codec_type?: string
    codec_name?: string
    profile?: string
    width?: number
    height?: number
    avg_frame_rate?: string
    r_frame_rate?: string
    pix_fmt?: string
    bits_per_raw_sample?: string
    channels?: number
    color_transfer?: string
    color_primaries?: string
    tags?: Record<string, string>
    disposition?: { default?: number; forced?: number; attached_pic?: number }
    side_data_list?: { side_data_type?: string }[]
    duration?: string
  }[]
}

export interface ProbeInterno {
  duracionSegundos: number
  contenedor: string
  tamanoBytes: number
  video: PistaVideo | null
  audio: PistaAudio[]
  /** Solo los internos; los externos se agregan aparte (dependen de los archivos que haya junto al video). */
  subtitulos: PistaSubtitulo[]
}

/** Subtítulos de texto convertibles a WebVTT; el resto de códecs de subtítulo son de imagen. */
const SUBTITULOS_TEXTO = new Set(['subrip', 'srt', 'ass', 'ssa', 'mov_text', 'webvtt', 'text', 'eia_608'])

/** "24000/1001" → 23.976 */
function fps(s: string | undefined): number | null {
  if (!s) return null
  const [n, d] = s.split('/').map(Number)
  if (!n || !d) return null
  return Math.round((n / d) * 1000) / 1000
}

function hdrDe(s: NonNullable<FfprobeJson['streams']>[number]): PistaVideo['hdr'] {
  if (s.side_data_list?.some((d) => /dovi|dolby/i.test(d.side_data_type ?? ''))) return 'dolbyvision'
  if (s.color_transfer === 'smpte2084') return 'hdr10'
  if (s.color_transfer === 'arib-std-b67') return 'hlg'
  return null
}

/** Profundidad de color: `bits_per_raw_sample` si viene; si no, se deduce del formato de píxel (`yuv420p10le` → 10). */
function bitsDe(s: NonNullable<FfprobeJson['streams']>[number]): number | null {
  const crudo = Number(s.bits_per_raw_sample)
  if (crudo > 0) return crudo
  const m = /p(10|12|14|16)/.exec(s.pix_fmt ?? '')
  if (m) return Number(m[1])
  return s.pix_fmt ? 8 : null
}

/** Nombre de contenedor legible a partir de `format_name` (ffprobe da listas como «mov,mp4,m4a,3gp,3g2,mj2»). */
export function contenedorDe(formatName: string | undefined, rutaArchivo: string): string {
  const ext = extname(rutaArchivo).toLowerCase().replace('.', '')
  const f = formatName ?? ''
  if (f.includes('matroska')) return ext === 'webm' ? 'webm' : 'matroska'
  if (f.includes('mov') || f.includes('mp4')) return ext === 'mov' ? 'mov' : 'mp4'
  if (f.includes('mpegts')) return 'mpegts'
  return f.split(',')[0] || ext || 'desconocido'
}

/**
 * Traduce la salida de ffprobe al resumen de `playback-info`. Los ids de pista son 1-based por tipo en el orden
 * del contenedor, que es la numeración de mpv: así el progreso guardado por la versión Flutter (`pista_audio_id`,
 * `pista_subtitulo_id`) sigue apuntando a la misma pista.
 */
export function parsearFfprobe(json: FfprobeJson, rutaArchivo: string, tamanoBytes: number): ProbeInterno {
  const streams = json.streams ?? []
  const v = streams.find((s) => s.codec_type === 'video' && s.disposition?.attached_pic !== 1) // ignora carátulas
  const video: PistaVideo | null = v
    ? {
        codec: v.codec_name ?? 'desconocido',
        perfil: v.profile ?? null,
        ancho: v.width ?? 0,
        alto: v.height ?? 0,
        hdr: hdrDe(v),
        fps: fps(v.avg_frame_rate) ?? fps(v.r_frame_rate),
        bits: bitsDe(v)
      }
    : null

  const audio: PistaAudio[] = streams
    .filter((s) => s.codec_type === 'audio')
    .map((s, i) => ({
      id: String(i + 1),
      indice: s.index,
      codec: s.codec_name ?? 'desconocido',
      canales: s.channels ?? 2,
      idioma: s.tags?.language && s.tags.language !== 'und' ? s.tags.language : null,
      titulo: s.tags?.title ?? null,
      defecto: s.disposition?.default === 1
    }))
  // Si ninguna se marcó por defecto, la primera lo es (así se comporta mpv).
  if (audio.length && !audio.some((a) => a.defecto)) audio[0].defecto = true

  const subtitulos: PistaSubtitulo[] = streams
    .filter((s) => s.codec_type === 'subtitle')
    .map((s, i) => ({
      id: String(i + 1),
      indice: s.index,
      codec: s.codec_name ?? 'desconocido',
      idioma: s.tags?.language && s.tags.language !== 'und' ? s.tags.language : null,
      titulo: s.tags?.title ?? null,
      forzado: s.disposition?.forced === 1,
      tipo: SUBTITULOS_TEXTO.has(s.codec_name ?? '') ? 'texto' : 'imagen',
      externo: false
    }))

  const duracion = Number(json.format?.duration ?? v?.duration ?? 0)
  return {
    duracionSegundos: Number.isFinite(duracion) ? Math.round(duracion * 10) / 10 : 0,
    contenedor: contenedorDe(json.format?.format_name, rutaArchivo),
    tamanoBytes,
    video,
    audio,
    subtitulos
  }
}
