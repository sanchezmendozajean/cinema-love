import type { ModoPlayback, PistaAudio, PistaSubtitulo, PistaVideo } from '../../shared/playback-types'

export interface CapacidadesFfmpeg {
  /** Filtro `zscale` disponible (necesario para el tone-mapping HDR → SDR). */
  zscale: boolean
  /** Nombres de encoders presentes en el build (`libx264`, `h264_nvenc`…). */
  encoders: Set<string>
  /** Encoders por hardware que se comprobó que funcionan en este equipo (`undefined` = todavía no se probó). */
  hwFuncionales?: Set<string>
}

export type AceleracionHw = 'off' | 'auto' | 'nvenc' | 'qsv' | 'amf' | 'vaapi'

export interface OpcionesFfmpeg {
  modo: Exclude<ModoPlayback, 'direct'>
  ruta: string
  inicioSegundos: number
  /**
   * Solo remux con salto: segundo REAL donde arranca el video copiado (el keyframe anterior a `inicioSegundos`, ver
   * `argsInicioRemux`). El audio se recodifica y `-ss` lo corta justo en lo pedido; sin esto el video iría por delante del
   * audio tanto como se haya retrocedido hasta el keyframe (segundos). Con él, el audio se abre desde el mismo punto.
   */
  inicioRealRemux?: number
  video: PistaVideo | null
  audio: PistaAudio | null
  /** Subtítulo de imagen a quemar en el video (solo transcode). */
  subtituloQuemado: PistaSubtitulo | null
  /** El audio elegido ya es AAC estéreo-compatible: en remux se copia sin recodificar. */
  audioCompatible: boolean
  maxAltura?: number
  hw: AceleracionHw
  caps: CapacidadesFfmpeg
  /** Para reintentar con software si el encoder por hardware falla. */
  forzarSoftware?: boolean
}

const TONEMAP =
  'zscale=t=linear:npl=100,format=gbrpf32le,zscale=p=bt709,tonemap=tonemap=hable:desat=0,zscale=t=bt709:m=bt709:r=tv,format=yuv420p'

export type IdEncoderHw = Exclude<AceleracionHw, 'off' | 'auto'>

/** Encoders H.264 por hardware que sabemos usar, en el orden en que «Automática» los prueba. */
export const ENCODERS_HW: { id: IdEncoderHw; nombre: string; fabricante: string; args: string[] }[] = [
  { id: 'nvenc', nombre: 'h264_nvenc', fabricante: 'NVIDIA (NVENC)', args: ['-c:v', 'h264_nvenc', '-preset', 'p4', '-cq', '23', '-pix_fmt', 'yuv420p'] },
  { id: 'qsv', nombre: 'h264_qsv', fabricante: 'Intel (Quick Sync)', args: ['-c:v', 'h264_qsv', '-global_quality', '23', '-pix_fmt', 'nv12'] },
  { id: 'amf', nombre: 'h264_amf', fabricante: 'AMD (AMF)', args: ['-c:v', 'h264_amf', '-quality', 'speed', '-pix_fmt', 'yuv420p'] },
  { id: 'vaapi', nombre: 'h264_vaapi', fabricante: 'VAAPI (Linux)', args: ['-c:v', 'h264_vaapi', '-qp', '23'] }
]

/** Encoder de video para transcode según el ajuste y lo que traiga el build; siempre cae a libx264. */
export function elegirEncoder(hw: AceleracionHw, caps: CapacidadesFfmpeg, forzarSoftware = false): { nombre: string; args: string[] } {
  const software = { nombre: 'libx264', args: ['-c:v', 'libx264', '-preset', 'veryfast', '-crf', '22', '-profile:v', 'high', '-pix_fmt', 'yuv420p'] }
  if (forzarSoftware || hw === 'off') return software
  const candidatos = hw === 'auto' ? ENCODERS_HW : ENCODERS_HW.filter((e) => e.id === hw)
  for (const e of candidatos) {
    if (!caps.encoders.has(e.nombre)) continue
    // «Automática» solo usa los que se comprobó que funcionan en ESTE equipo (si ya se probó); uno elegido a mano se respeta.
    if (hw === 'auto' && caps.hwFuncionales && !caps.hwFuncionales.has(e.nombre)) continue
    return e
  }
  return software
}

/**
 * Codificación de prueba de 1 s (720p) con un encoder por hardware: el que de verdad funciona en este equipo es el que
 * codifica sin error. Que el build de ffmpeg «lo traiga» no basta: sin esa GPU el encoder existe pero falla al abrirse.
 */
export function argsPruebaEncoder(e: (typeof ENCODERS_HW)[number]): string[] {
  const vaapi = e.id === 'vaapi'
  return [
    '-v', 'error', '-nostdin',
    ...(vaapi ? ['-vaapi_device', '/dev/dri/renderD128'] : []),
    '-f', 'lavfi', '-i', 'testsrc2=size=1280x720:rate=30:duration=1',
    ...(vaapi ? ['-vf', 'format=nv12,hwupload'] : []),
    '-frames:v', '30', ...e.args, '-f', 'null', '-'
  ]
}

/**
 * Argumentos de ffmpeg para una sesión remux/transcode: fMP4 fragmentado a stdout (specs/05 §1).
 * - remux: `-c:v copy`; audio a AAC estéreo salvo que ya sea compatible.
 * - transcode: H.264 + AAC; subtítulo de imagen quemado con `overlay`; HDR → SDR con tone-mapping si hay `zscale`.
 */
export function construirArgsFfmpeg(o: OpcionesFfmpeg): string[] {
  const args = ['-hide_banner', '-loglevel', 'error', '-nostdin']
  if (o.inicioSegundos > 0) args.push('-ss', String(Math.round(o.inicioSegundos * 1000) / 1000))
  args.push('-i', o.ruta)
  // Remux con salto: segunda entrada, el mismo archivo, para el audio desde donde empieza de verdad el video copiado.
  const audioAparte = o.modo === 'remux' && !!o.video && !!o.audio && o.inicioSegundos > 0 && o.inicioRealRemux != null && o.inicioRealRemux > 0 && o.inicioRealRemux < o.inicioSegundos
  if (audioAparte) args.push('-ss', String(Math.round(o.inicioRealRemux! * 1000) / 1000), '-i', o.ruta)

  const encoder = o.modo === 'transcode' ? elegirEncoder(o.hw, o.caps, o.forzarSoftware) : null
  const necesitaFiltroVideo = o.modo === 'transcode'
  const filtros: string[] = []
  if (o.modo === 'transcode' && o.video) {
    if (o.video.hdr && o.caps.zscale) filtros.push(TONEMAP)
    if (o.maxAltura && o.video.alto > o.maxAltura) filtros.push(`scale=-2:${o.maxAltura}`)
    if (encoder?.nombre === 'h264_vaapi') filtros.push('format=nv12', 'hwupload')
  }

  const quemar = o.modo === 'transcode' && o.subtituloQuemado?.indice != null ? o.subtituloQuemado.indice : null
  if (necesitaFiltroVideo && quemar != null) {
    // Subtítulo de imagen: se superpone al video (antes de escalar, así encaja con el tamaño original).
    const cadena = filtros.length ? `;[v0]${filtros.join(',')}[v]` : ''
    args.push('-filter_complex', `[0:v:0][0:${quemar}]overlay${filtros.length ? '[v0]' : '[v]'}${cadena}`, '-map', '[v]')
  } else {
    if (o.video) args.push('-map', '0:v:0')
    if (filtros.length) args.push('-vf', filtros.join(','))
  }
  if (o.audio) args.push('-map', `${audioAparte ? 1 : 0}:${o.audio.indice}`)

  if (o.modo === 'remux') {
    if (o.video) args.push('-c:v', 'copy')
  } else if (encoder) {
    if (encoder.nombre === 'h264_vaapi') args.unshift('-vaapi_device', '/dev/dri/renderD128')
    args.push(...encoder.args)
  }

  if (o.audio) {
    if (o.modo === 'remux' && o.audioCompatible) args.push('-c:a', 'copy')
    else args.push('-c:a', 'aac', '-ac', '2', '-b:a', '192k')
  }

  // Sin subtítulos ni datos en el contenedor de salida: los de texto los muestra el overlay de la app.
  args.push('-sn', '-dn', '-f', 'mp4', '-movflags', 'frag_keyframe+empty_moov+default_base_moof', 'pipe:1')
  return args
}

/**
 * Dónde arrancaría de verdad un remux con salto. Un remux copia el video, así que `-ss` no cae en el instante pedido sino en
 * el punto de búsqueda del contenedor anterior (en un MKV, el último Cue, que puede estar un GOP o más antes). Esta
 * consulta hace EXACTAMENTE esa misma búsqueda, saca el primer paquete (`framecrc`) con sus marcas de tiempo originales
 * (`-copyts`) y así sabemos cuál es el segundo real: posición, progreso y subtítulos externos dependen de él.
 */
export function argsInicioRemux(ruta: string, inicioSegundos: number): string[] {
  return ['-v', 'error', '-nostdin', '-ss', String(Math.round(inicioSegundos * 1000) / 1000), '-i', ruta, '-map', '0:v:0', '-c', 'copy', '-copyts', '-frames:v', '1', '-f', 'framecrc', '-']
}

/**
 * Segundo del archivo donde arranca el remux, a partir de la salida de `argsInicioRemux`. Se usa el DTS del primer paquete:
 * el stream de salida empieza en 0 en su DTS (el primer fotograma se muestra un instante después, por el reordenamiento de
 * los B-frames), de modo que `posiciónReal = offset + currentTime`. `null` si no se pudo leer.
 */
export function parsearInicioRemux(salida: string): number | null {
  const base = /^#tb 0: (\d+)\/(\d+)/m.exec(salida)
  const paquete = /^0,\s*(-?\d+),\s*(-?\d+),/m.exec(salida)
  if (!base || !paquete) return null
  const num = Number(base[1])
  const den = Number(base[2])
  const dts = Number(paquete[1])
  if (!(den > 0) || !Number.isFinite(dts)) return null
  return Math.max(0, Math.round((dts * num * 1000) / den) / 1000)
}

/** Argumentos para convertir una pista de subtítulos (interna o un archivo) a WebVTT. */
export function argsSubtituloAVtt(entrada: string, indiceStream: number | null, salida: string): string[] {
  const args = ['-hide_banner', '-loglevel', 'error', '-nostdin', '-y', '-i', entrada]
  if (indiceStream != null) args.push('-map', `0:${indiceStream}`)
  args.push('-f', 'webvtt', salida)
  return args
}

/** Lee `ffmpeg -encoders` y `ffmpeg -filters` para saber qué trae el build. */
export function parsearCapacidades(salidaEncoders: string, salidaFiltros: string): CapacidadesFfmpeg {
  const encoders = new Set<string>()
  for (const l of salidaEncoders.split('\n')) {
    const m = /^\s*[VAS][A-Z.]{5}\s+(\S+)/.exec(l)
    if (m) encoders.add(m[1])
  }
  const zscale = salidaFiltros.split('\n').some((l) => /^\s*\S+\s+zscale\s/.test(l))
  return { zscale, encoders }
}
