/** Tipos de reproducción compartidos main ↔ renderer (specs/05-reproductor.md §3). */

export type ModoPlayback = 'direct' | 'remux' | 'transcode'

export interface PistaVideo {
  codec: string
  perfil: string | null
  ancho: number
  alto: number
  /** `null` = SDR. */
  hdr: 'hdr10' | 'hlg' | 'dolbyvision' | null
  fps: number | null
  /** 8 o 10 (profundidad de color); null si no se sabe. */
  bits: number | null
}

export interface PistaAudio {
  /** Id 1-based por tipo en el orden del contenedor (compatible con mpv). */
  id: string
  /** Índice absoluto del stream en el contenedor (ffmpeg `0:<indice>`). */
  indice: number
  codec: string
  canales: number
  idioma: string | null
  titulo: string | null
  defecto: boolean
}

export interface PistaSubtitulo {
  /** `"1"`… para internos (1-based por tipo, como mpv) o `"ext-1"`… para archivos externos. */
  id: string
  indice: number | null
  codec: string
  idioma: string | null
  titulo: string | null
  forzado: boolean
  /** `imagen` (PGS, VobSub, DVB) obliga a quemarlo en el video → transcode. */
  tipo: 'texto' | 'imagen'
  externo: boolean
}

export interface DecisionPlayback {
  modo: ModoPlayback
  motivo: string
}

/** Lo que el cliente sabe reproducir; el servidor decide con esto (un cliente Android tendrá otras). */
export interface CapacidadesCliente {
  video: string[]
  audio: string[]
  contenedores: string[]
}

export const CAPACIDADES_BASE: CapacidadesCliente = {
  video: ['h264', 'vp8', 'vp9', 'av1'],
  audio: ['aac', 'mp3', 'opus', 'vorbis', 'flac'],
  contenedores: ['mp4', 'mov', 'webm']
}

export interface PlaybackInfo {
  duracionSegundos: number
  contenedor: string
  tamanoBytes: number
  video: PistaVideo | null
  audio: PistaAudio[]
  subtitulos: PistaSubtitulo[]
  decision: DecisionPlayback
}

export interface SesionRequest {
  mediaItemId?: string
  extraId?: string
  inicioSegundos?: number
  /** Id de pista de audio (`"1"`…); por defecto la marcada como predeterminada. */
  audio?: string
  /** Id de subtítulo a quemar en el video; los de texto NO se envían aquí (se muestran con el overlay). */
  subtitulo?: string
  /** Fuerza un modo (p. ej. «Reintentar con conversión» → transcode). */
  modo?: ModoPlayback
  maxAltura?: number
  /** Capacidades del cliente (como `?caps=` de playback-info, pero como objeto). */
  caps?: CapacidadesCliente
}

export interface SesionRespuesta {
  sessionId: string
  /** Ruta relativa de la API (el cliente le agrega base y credencial). */
  url: string
  modo: ModoPlayback
  /** Segundo del archivo en el que arranca el stream: `posiciónReal = offsetSegundos + video.currentTime`. */
  offsetSegundos: number
  motivo: string
}

// --- aceleración por hardware (Ajustes → Reproducción)

export type AceleracionHwAjuste = 'off' | 'auto' | 'nvenc' | 'qsv' | 'amf' | 'vaapi'

/** Resultado de probar un encoder por hardware con una codificación real de 1 s. */
export interface ResultadoEncoderHw {
  id: 'nvenc' | 'qsv' | 'amf' | 'vaapi'
  fabricante: string
  encoder: string
  /** El ffmpeg incluido trae ese encoder. */
  enBuild: boolean
  /** Codificó la prueba sin error en este equipo. */
  funciona: boolean
  /** Lo que tardó la prueba (solo orientativo). */
  ms: number | null
  /** Por qué falló, resumido. */
  error: string | null
}

export interface AjustesTranscodificacion {
  /** `false` si esta instalación no incluye ffmpeg: no hay transcodificación. */
  disponible: boolean
  hw: AceleracionHwAjuste
  maxTranscodes: number
  /** Calidad máxima (alto en píxeles) al convertir; 0 = la del original. Solo afecta a lo que se transcodifica. */
  maxAltura: number
  /** Último resultado de «Probar mi equipo» (null si no se ha probado en esta sesión). */
  prueba: ResultadoEncoderHw[] | null
}
