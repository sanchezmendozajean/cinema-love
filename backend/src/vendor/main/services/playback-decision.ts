import type { CapacidadesCliente, DecisionPlayback, PistaAudio, PistaVideo } from '../../shared/playback-types'
import { CAPACIDADES_BASE } from '../../shared/playback-types'

/** Lo mínimo del probe que hace falta para decidir. */
export interface EntradaDecision {
  contenedor: string
  video: PistaVideo | null
  audio: PistaAudio[]
}

const NOMBRES_AUDIO: Record<string, string> = {
  ac3: 'AC3',
  eac3: 'E-AC3',
  dts: 'DTS',
  truehd: 'TrueHD',
  mp2: 'MP2',
  wmav2: 'WMA',
  pcm_s16le: 'PCM'
}
const nombreAudio = (c: string): string => NOMBRES_AUDIO[c] ?? c.toUpperCase()

/**
 * ¿Chromium reproduce este video tal cual? H.264 solo hasta High 4:2:0 de 8 bits (nada de Hi10P ni 4:4:4);
 * el resto de códecs se aceptan si el cliente los declaró (HEVC depende del hardware/SO: se detecta en el cliente).
 */
export function videoCompatible(v: PistaVideo, caps: CapacidadesCliente): { ok: boolean; motivo?: string } {
  if (!caps.video.includes(v.codec)) return { ok: false, motivo: `Video ${v.codec.toUpperCase()} no compatible` }
  if (v.codec === 'h264') {
    if ((v.bits ?? 8) > 8) return { ok: false, motivo: 'Video H.264 de 10 bits no compatible' }
    if (/4:4:4|4:2:2|High 10|High 4:4:4|Hi10/i.test(v.perfil ?? '')) return { ok: false, motivo: `Perfil H.264 «${v.perfil}» no compatible` }
  }
  return { ok: true }
}

/** Audio dentro de un contenedor MP4/WebM; el contenedor limita qué códecs admite. */
function audioCompatible(a: PistaAudio | undefined, caps: CapacidadesCliente, contenedorSalida: string): boolean {
  if (!a) return true
  if (!caps.audio.includes(a.codec)) return false
  // WebM solo lleva Opus/Vorbis; FLAC va en MP4/Matroska.
  if (contenedorSalida === 'webm') return a.codec === 'opus' || a.codec === 'vorbis'
  return true
}

/**
 * Decide cómo reproducir (specs/05 §1):
 * - **direct**: contenedor y códecs los reproduce el cliente tal cual.
 * - **remux**: el video sirve pero el contenedor o el audio no → se copia el video y el audio pasa a AAC.
 * - **transcode**: el video no sirve → H.264 + AAC.
 * `audioElegido` es la pista de audio que se va a oír (por defecto, la predeterminada).
 */
export function decidirModo(info: EntradaDecision, caps: CapacidadesCliente = CAPACIDADES_BASE, audioElegido?: string): DecisionPlayback {
  if (!info.video) return { modo: 'remux', motivo: 'El archivo no tiene video: se prepara solo el audio' }
  const v = videoCompatible(info.video, caps)
  if (!v.ok) return { modo: 'transcode', motivo: v.motivo ?? 'Video no compatible' }

  const audio = info.audio.find((a) => a.id === audioElegido) ?? info.audio.find((a) => a.defecto) ?? info.audio[0]
  const contenedorOk = caps.contenedores.includes(info.contenedor)
  const audioOk = audioCompatible(audio, caps, info.contenedor)

  if (contenedorOk && audioOk) return { modo: 'direct', motivo: 'Archivo compatible: se reproduce directamente' }
  if (!contenedorOk && !audioOk && audio) return { modo: 'remux', motivo: `Contenedor ${info.contenedor} y audio ${nombreAudio(audio.codec)} no compatibles` }
  if (!audioOk && audio) return { modo: 'remux', motivo: `Audio ${nombreAudio(audio.codec)} no compatible` }
  return { modo: 'remux', motivo: `Contenedor ${info.contenedor} no compatible` }
}
