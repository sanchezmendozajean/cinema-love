import { spawn, type ChildProcess } from 'node:child_process'
import { createHash, randomBytes } from 'node:crypto'
import { existsSync, mkdirSync, readdirSync, rmSync, statSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'
import { Readable } from 'node:stream'
import type { ModoPlayback, PistaAudio, PistaSubtitulo, ResultadoEncoderHw, SesionRespuesta } from '../../shared/playback-types'
import { argsInicioRemux, argsPruebaEncoder, argsSubtituloAVtt, construirArgsFfmpeg, ENCODERS_HW, parsearCapacidades, parsearInicioRemux, type AceleracionHw, type CapacidadesFfmpeg } from './ffmpeg-args'
import { ejecutarProceso, type Ejecutar } from './media-probe'
import { leerTextoSubtitulo } from './subtitulos'

export class ErrorSesion extends Error {
  constructor(
    readonly status: number,
    mensaje: string
  ) {
    super(mensaje)
  }
}

/** Última línea útil de un error de ffmpeg, sin la ruta del ejecutable, para mostrarla en la interfaz. */
export function resumirErrorFfmpeg(texto: string): string {
  const limpio = texto
    .replace(/^.*?falló:\s*/i, '')
    .replace(/\[[^\]]*\]\s*/g, '') // «[vost#0:0/h264_qsv @ 00000191…]»: identificadores internos de ffmpeg
    .replace(/\s+/g, ' ')
    .trim()
  return limpio.length > 120 ? limpio.slice(0, 117) + '…' : limpio || 'Error desconocido'
}

export type Lanzar = (comando: string, args: string[]) => ChildProcess

const lanzarReal: Lanzar = (comando, args) => spawn(comando, args, { stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true })

interface Sesion {
  id: string
  /** Quién la creó (tipo + usuario): solo él puede leerla o detenerla. */
  propietario: string
  modo: Exclude<ModoPlayback, 'direct'>
  proc: ChildProcess
  /** stdout del proceso actual (cambia si se reintenta con software). */
  stdout: Readable
  /** Lo último que produjo ffmpeg (ventana deslizante): Chromium reabre el stream con `Range` cerca de donde reproduce. */
  ventana: Buffer[]
  /** Posición absoluta (en bytes del stream) del primer trozo de `ventana`. */
  inicioVentana: number
  /**
   * Por dónde va reproduciendo el cliente, según el último `Range` que pidió (Chromium reabre el stream cerca de su
   * posición de reproducción). Se usa para no entregarle más que `ADELANTO_CLIENTE` por delante y para saber qué conservar.
   */
  refReproduccion: number
  /** Cuándo se fijó `refReproduccion` (la reproducción sigue avanzando desde ahí aunque el cliente no vuelva a pedir nada). */
  refTiempo: number
  /** Bytes por segundo con que se estima cuánto ha reproducido el cliente desde `refTiempo` (por exceso, nunca por defecto). */
  ritmo: number
  /** Bytes producidos en total: si un encoder por hardware falla antes de emitir nada, se reintenta con software. */
  bytes: number
  /** ffmpeg ya terminó: los lectores acaban al llegar al final. */
  terminada: boolean
  /** Conexiones abiertas ahora mismo (Chromium abre la nueva antes de cerrar la vieja). */
  lectores: Set<LectorSesion>
  /** Posición hasta la que algún cliente ya sacó del buffer (para detectar si sigue leyendo). */
  consumido: number
  ultimaLectura: number
  /** Alguien llegó a leer alguna vez (para no dejar colgada una sesión que nadie abre). */
  abierta: boolean
  /** Sin lectores, la sesión espera un instante a que el cliente reabra antes de detenerse. */
  gracia?: NodeJS.Timeout
  stderr: string
  /** Para el panel «Reproduciendo ahora». */
  etiqueta?: EtiquetaSesion
  motivo: string
  inicio: number
}

/** Tope de lo producido que se conserva para servir reaperturas (`Range`) del navegador. */
const VENTANA_MAX = 384 * 1024 * 1024
/**
 * Lo que se conserva por detrás de la última posición pedida por el cliente. Esa posición (`Range` de la última reapertura)
 * suele ser el FINAL de lo que Chromium ya tiene en buffer, que va por delante de lo que reproduce: con un margen corto,
 * al volver a pedir bytes cercanos a la reproducción ya estaban descartados (416) y la reproducción se reiniciaba con una pausa
 * y un retroceso. Debe cubrir al menos el adelanto que Chromium puede tener en buffer.
 */
const MARGEN_ATRAS = 128 * 1024 * 1024
/**
 * Cuánto se le entrega al cliente por delante de su posición de reproducción. Con `Accept-Ranges: bytes` Chromium lee tan
 * rápido como se le dé (cientos de MB en segundos) y descarta lo viejo: hay que frenarlo para que lo que vuelva a pedir
 * siga dentro de la ventana.
 */
const ADELANTO_CLIENTE = 96 * 1024 * 1024
/** Ritmo mínimo (bytes/s) con el que se estima la reproducción: más vale soltar de más que dejar al cliente sin datos. */
const RITMO_MIN = 3 * 1024 * 1024
/** Cuánto puede adelantarse ffmpeg al lector más avanzado antes de pausarlo. */
const ADELANTO_MAX = 32 * 1024 * 1024
/**
 * Cuánto vive una sesión SIN ninguna conexión abierta (el navegador puede soltar y reabrir la suya). Mientras haya una
 * conexión abierta no caduca, por larga que sea la pausa: ffmpeg en pausa no consume CPU (el stream aplica contrapresión)
 * y al dar play hay que seguir donde estaba, porque reiniciar un remux obliga a arrancar en el keyframe anterior (hasta
 * ~10 s antes). Cerrar el reproductor la detiene al instante (DELETE) y una app caída cierra la conexión.
 */
const INACTIVIDAD_MS = 30 * 60_000

/** Una conexión de lectura al stream de una sesión, desde una posición absoluta (`Range: bytes=N-`). */
export class LectorSesion extends Readable {
  pos: number
  private reintento?: NodeJS.Timeout
  constructor(
    private readonly s: Sesion,
    desde: number,
    private readonly alAvanzar: () => void
  ) {
    super({ highWaterMark: 256 * 1024 })
    this.pos = desde
  }

  override _destroy(err: Error | null, cb: (error?: Error | null) => void): void {
    clearTimeout(this.reintento)
    cb(err)
  }

  /** Hay datos nuevos (o ffmpeg terminó): reintenta. */
  despertar(): void {
    if (!this.destroyed) this._read()
  }

  override _read(): void {
    const s = this.s
    for (;;) {
      if (this.pos < s.inicioVentana) return void this.destroy(new ErrorSesion(416, 'La posición pedida ya salió de la ventana de la sesión.'))
      if (this.pos >= s.bytes) {
        if (s.terminada) this.push(null)
        return // sin datos todavía: `despertar` lo retoma
      }
      // Ya tiene de sobra por delante. La posición de reproducción se ESTIMA por el tiempo transcurrido desde la última
      // referencia: si solo avanzara cuando el cliente reabre con Range, un cliente que no reabre se quedaría sin datos
      // al alcanzar el tope y la reproducción se colgaría para siempre.
      const estimada = s.refReproduccion + ((Date.now() - s.refTiempo) / 1000) * s.ritmo
      if (this.pos - estimada > ADELANTO_CLIENTE) {
        this.reintento ??= setTimeout(() => {
          this.reintento = undefined
          this.despertar()
        }, 500)
        return
      }
      let inicio = s.inicioVentana
      let i = 0
      while (i < s.ventana.length && inicio + s.ventana[i].length <= this.pos) inicio += s.ventana[i++].length
      const trozo = s.ventana[i].subarray(this.pos - inicio)
      this.pos += trozo.length
      this.alAvanzar()
      if (!this.push(trozo)) return
    }
  }
}

export interface OpcionesPlayback {
  ffmpeg: string
  /** Carpeta de temporales (`transcode/`): subtítulos convertidos. Se limpia al arrancar. */
  carpetaTemporal: string
  lanzar?: Lanzar
  ejecutar?: Ejecutar
  /** Se consulta en cada sesión (los ajustes pueden cambiar con la app abierta). */
  ajustes?: () => { hw: AceleracionHw; maxTranscodes: number }
  /** Sin ninguna conexión abierta durante este tiempo la sesión se detiene (30 min por defecto). */
  inactividadMs?: number
}

/** Qué se está reproduciendo y quién lo pidió (solo informativo). */
export interface EtiquetaSesion {
  titulo: string
  quien: string
  cliente: string
}

export interface SesionActiva extends EtiquetaSesion {
  id: string
  modo: Exclude<ModoPlayback, 'direct'>
  motivo: string
  inicio: number
}

export interface ParametrosSesion {
  etiqueta?: EtiquetaSesion
  propietario: string
  modo: Exclude<ModoPlayback, 'direct'>
  ruta: string
  inicioSegundos: number
  video: import('../../shared/playback-types').PistaVideo | null
  audio: PistaAudio | null
  subtituloQuemado: PistaSubtitulo | null
  audioCompatible: boolean
  maxAltura?: number
  motivo: string
  /** Bytes por segundo del archivo (tamaño / duración), para estimar la reproducción del cliente. */
  bytesPorSegundo?: number
}

/**
 * Sesiones de remux/transcode con ffmpeg (specs/05 §1): el proceso escribe un fMP4 fragmentado a stdout y la API lo
 * entrega por HTTP. Una sesión muere con `DELETE` o tras 30 min sin ninguna conexión abierta. Máx. 2 transcodes
 * simultáneos (configurable); los extra reciben 503 con un mensaje claro.
 */
export class PlaybackService {
  private readonly sesiones = new Map<string, Sesion>()
  private readonly lanzar: Lanzar
  private readonly ejecutar: Ejecutar
  private readonly inactividadMs: number
  private capacidades: Promise<CapacidadesFfmpeg> | null = null
  private pruebaHw: Promise<ResultadoEncoderHw[]> | null = null
  private hwFuncionales: Set<string> | undefined
  private resultadosHw: ResultadoEncoderHw[] | null = null

  /** Último resultado de `probarAceleracion` en esta sesión (null si aún no se probó). */
  get ultimaPruebaHw(): ResultadoEncoderHw[] | null {
    return this.resultadosHw
  }
  private readonly barrido: NodeJS.Timeout

  constructor(private readonly o: OpcionesPlayback) {
    this.lanzar = o.lanzar ?? lanzarReal
    this.ejecutar = o.ejecutar ?? ejecutarProceso
    this.inactividadMs = o.inactividadMs ?? INACTIVIDAD_MS
    this.limpiarTemporales()
    this.barrido = setInterval(() => this.detenerInactivas(), Math.min(5000, Math.max(50, this.inactividadMs / 4)))
    this.barrido.unref()
  }

  /** Qué trae este build de ffmpeg (zscale, encoders por hardware); se consulta una vez. */
  capacidadesFfmpeg(): Promise<CapacidadesFfmpeg> {
    this.capacidades ??= Promise.all([
      this.ejecutar(this.o.ffmpeg, ['-hide_banner', '-encoders']),
      this.ejecutar(this.o.ffmpeg, ['-hide_banner', '-filters'])
    ])
      .then(([enc, fil]) => parsearCapacidades(enc, fil))
      .catch(() => ({ zscale: false, encoders: new Set<string>() }))
    return this.capacidades
  }

  /**
   * Prueba cada encoder por hardware que trae el build con una codificación real de 1 s y devuelve cuáles funcionan en ESTE
   * equipo (y cuánto tardó). El resultado se guarda: «Automática» solo usa los que funcionan. `forzar` repite la prueba
   * (p. ej. tras instalar un driver o conectar una GPU).
   */
  probarAceleracion(forzar = false): Promise<ResultadoEncoderHw[]> {
    if (forzar) this.pruebaHw = null
    this.pruebaHw ??= (async () => {
      const caps = await this.capacidadesFfmpeg()
      const resultados: ResultadoEncoderHw[] = []
      for (const e of ENCODERS_HW) {
        if (e.id === 'vaapi' && process.platform !== 'linux') continue // VAAPI no existe fuera de Linux
        const enBuild = caps.encoders.has(e.nombre)
        if (!enBuild) {
          resultados.push({ id: e.id, fabricante: e.fabricante, encoder: e.nombre, enBuild: false, funciona: false, ms: null, error: null })
          continue
        }
        const t0 = Date.now()
        try {
          await this.ejecutar(this.o.ffmpeg, argsPruebaEncoder(e), { timeoutMs: 20_000 })
          resultados.push({ id: e.id, fabricante: e.fabricante, encoder: e.nombre, enBuild: true, funciona: true, ms: Date.now() - t0, error: null })
        } catch (err) {
          const texto = err instanceof Error ? err.message : String(err)
          resultados.push({ id: e.id, fabricante: e.fabricante, encoder: e.nombre, enBuild: true, funciona: false, ms: null, error: resumirErrorFfmpeg(texto) })
        }
      }
      this.hwFuncionales = new Set(resultados.filter((r) => r.funciona).map((r) => r.encoder))
      this.resultadosHw = resultados
      return resultados
    })()
    return this.pruebaHw
  }

  get activas(): number {
    return this.sesiones.size
  }

  private transcodesActivos(): number {
    return [...this.sesiones.values()].filter((s) => s.modo === 'transcode').length
  }

  async crear(p: ParametrosSesion): Promise<SesionRespuesta> {
    const ajustes = this.o.ajustes?.() ?? { hw: 'off' as AceleracionHw, maxTranscodes: 2 }
    if (p.modo === 'transcode' && this.transcodesActivos() >= ajustes.maxTranscodes) {
      throw new ErrorSesion(
        503,
        `El servidor ya está convirtiendo ${ajustes.maxTranscodes} videos a la vez. Espera a que termine alguno e inténtalo de nuevo.`
      )
    }
    // «Automática» solo usa encoders por hardware que se comprobó que funcionan aquí: la primera vez se prueban (unos segundos).
    if (p.modo === 'transcode' && ajustes.hw === 'auto') await this.probarAceleracion()
    const caps = { ...(await this.capacidadesFfmpeg()), hwFuncionales: this.hwFuncionales }
    // En remux el salto cae donde el contenedor puede buscar, no donde se pidió: se informa el segundo REAL de arranque.
    const offsetSegundos = p.modo === 'remux' && p.video && p.inicioSegundos > 0 ? await this.inicioRealRemux(p.ruta, p.inicioSegundos) : p.inicioSegundos
    const id = randomBytes(12).toString('hex')
    const lanzarProceso = (forzarSoftware: boolean): ChildProcess => this.lanzar(this.o.ffmpeg, construirArgsFfmpeg({ ...p, inicioRealRemux: p.modo === 'remux' ? offsetSegundos : undefined, hw: ajustes.hw, caps, forzarSoftware }))

    // La salida de la sesión es UNA sola y estable: cada proceso de ffmpeg alimenta la misma ventana. Así, si el encoder por
    // hardware falla de entrada y se reintenta con libx264, quien ya está leyendo no se entera.
    const sesion: Sesion = {
      id,
      propietario: p.propietario,
      etiqueta: p.etiqueta,
      motivo: p.motivo,
      inicio: Date.now(),
      modo: p.modo,
      proc: lanzarProceso(false),
      stdout: undefined as unknown as Readable,
      ultimaLectura: Date.now(),
      abierta: false,
      stderr: '',
      ventana: [],
      inicioVentana: 0,
      refReproduccion: 0,
      refTiempo: Date.now(),
      ritmo: Math.max(RITMO_MIN, 2 * (p.bytesPorSegundo ?? 0)),
      bytes: 0,
      terminada: false,
      lectores: new Set(),
      consumido: 0
    }
    const usaHardware = p.modo === 'transcode' && ajustes.hw !== 'off'
    const conectar = (proc: ChildProcess, puedeReintentar: boolean): void => {
      sesion.proc = proc
      sesion.stderr = ''
      proc.stdout!.on('data', (c: Buffer) => this.agregar(sesion, c))
      proc.stderr?.on('data', (c: Buffer) => (sesion.stderr = (sesion.stderr + c.toString()).slice(-2000)))
      proc.once('error', () => this.detener(id))
      proc.once('close', (codigo) => {
        if (this.sesiones.get(id) !== sesion || sesion.proc !== proc) return // sesión detenida o proceso reemplazado
        if (puedeReintentar && codigo && sesion.bytes === 0) return conectar(lanzarProceso(true), false) // el hardware falló: software
        // ffmpeg acabó solo (no lo detuvimos): si fue con error, queda en el registro para entender cortes de reproducción.
        if (codigo) console.warn(`[reproducción] ffmpeg terminó con código ${codigo} tras ${sesion.bytes} bytes (${Math.round((Date.now() - sesion.inicio) / 1000)} s): ${sesion.stderr.trim().slice(-300)}`)
        sesion.terminada = true
        for (const l of sesion.lectores) l.despertar()
        // Si nadie llega a leerla, no queda colgada.
        setTimeout(() => this.sesiones.get(id) === sesion && !sesion.abierta && this.detener(id), this.inactividadMs).unref()
      })
    }
    this.sesiones.set(id, sesion)
    conectar(sesion.proc, usaHardware)

    return {
      sessionId: id,
      url: `/api/v1/playback/sessions/${id}/stream`,
      modo: p.modo,
      offsetSegundos,
      motivo: p.motivo
    }
  }

  /** Segundo real donde arrancará un remux pedido en `inicio`; si no se puede averiguar, el pedido (es lo mejor que hay). */
  private async inicioRealRemux(ruta: string, inicio: number): Promise<number> {
    try {
      return parsearInicioRemux(await this.ejecutar(this.o.ffmpeg, argsInicioRemux(ruta, inicio), { timeoutMs: 10_000 })) ?? inicio
    } catch {
      return inicio
    }
  }

  /**
   * Abre una conexión de lectura al stream de la sesión desde `desde` (bytes). Chromium reabre el stream con `Range` cerca
   * de donde reproduce, a veces antes de cerrar la anterior: se admiten varios lectores a la vez. Si la sesión se queda sin
   * ninguno, espera unos segundos a que el cliente reabra antes de detener ffmpeg.
   */
  abrirStream(id: string, propietario: string, desde = 0): LectorSesion {
    const s = this.sesiones.get(id)
    if (!s) throw new ErrorSesion(404, 'Sesión de reproducción no encontrada o vencida.')
    if (s.propietario !== propietario) throw new ErrorSesion(403, 'Esta sesión pertenece a otra conexión.')
    if (desde < s.inicioVentana) {
      const mb = (n: number): string => `${Math.round(n / 1048576)} MB`
      console.warn(
        `[reproducción] 416 en la sesión ${id.slice(0, 6)}: pedido=${mb(desde)} inicioVentana=${mb(s.inicioVentana)} (${mb(s.inicioVentana - desde)} fuera) ` +
          `refReproduccion=${mb(s.refReproduccion)} producido=${mb(s.bytes)} lectores=${s.lectores.size} hace ${Math.round((Date.now() - s.inicio) / 1000)} s`
      )
      throw new ErrorSesion(416, 'La posición pedida ya salió de la ventana de la sesión.')
    }
    s.abierta = true
    s.ultimaLectura = Date.now()
    clearTimeout(s.gracia)
    if (desde > 0) {
      s.refReproduccion = desde
      s.refTiempo = Date.now()
      for (const l of s.lectores) l.despertar() // quien iba frenado puede seguir: la reproducción avanzó
    }
    const lector = new LectorSesion(s, desde, () => this.reanudarSiHaceFalta(s))
    s.lectores.add(lector)
    lector.once('close', () => {
      s.lectores.delete(lector)
      if (s.lectores.size === 0 && this.sesiones.get(id) === s) {
        s.ultimaLectura = Date.now() // desde aquí cuenta el plazo (la pausa que hubo con la conexión abierta no cuenta)
        s.gracia = setTimeout(() => s.lectores.size === 0 && this.detener(id), this.inactividadMs)
        s.gracia.unref()
      }
    })
    return lector
  }

  /** Guarda lo que produjo ffmpeg, recorta la ventana y pausa a ffmpeg si va demasiado por delante de los lectores. */
  private agregar(s: Sesion, chunk: Buffer): void {
    s.ventana.push(chunk)
    s.bytes += chunk.length
    this.recortarVentana(s)
    for (const l of s.lectores) l.despertar()
    if (s.bytes - this.posMasAvanzada(s) > ADELANTO_MAX) s.proc.stdout?.pause()
  }

  /** Descarta lo que quedó muy por detrás de la reproducción (o lo que exceda el tope). */
  private recortarVentana(s: Sesion): void {
    while (s.ventana.length > 1) {
      const fin = s.inicioVentana + s.ventana[0].length
      const muyAtras = fin <= s.refReproduccion - MARGEN_ATRAS
      const sobra = s.bytes - fin >= VENTANA_MAX
      if (!muyAtras && !sobra) break
      s.inicioVentana = fin
      s.ventana.shift()
    }
  }

  private posMasAvanzada(s: Sesion): number {
    let max = 0
    for (const l of s.lectores) max = Math.max(max, l.pos)
    return max
  }

  private reanudarSiHaceFalta(s: Sesion): void {
    if (s.bytes - this.posMasAvanzada(s) <= ADELANTO_MAX) s.proc.stdout?.resume()
  }

  /** Último error de ffmpeg de una sesión (para mostrar un detalle técnico si el video no arranca). */
  errorDe(id: string): string {
    return this.sesiones.get(id)?.stderr.trim() ?? ''
  }

  /** Sesiones de remux/transcode en curso (las «direct» no abren sesión). */
  listarSesiones(): SesionActiva[] {
    return [...this.sesiones.values()].map((s) => ({ id: s.id, modo: s.modo, motivo: s.motivo, inicio: s.inicio, titulo: s.etiqueta?.titulo ?? '—', quien: s.etiqueta?.quien ?? '—', cliente: s.etiqueta?.cliente ?? '—' }))
  }

  detener(id: string, propietario?: string): boolean {
    const s = this.sesiones.get(id)
    if (!s) return false
    if (propietario !== undefined && s.propietario !== propietario) throw new ErrorSesion(403, 'Esta sesión pertenece a otra conexión.')
    this.sesiones.delete(id)
    try {
      clearTimeout(s.gracia)
      for (const l of [...s.lectores]) l.destroy()
      s.proc.stdout?.destroy()
      s.proc.kill('SIGKILL')
    } catch {
      // ya había terminado
    }
    return true
  }

  /**
   * Detiene las sesiones abandonadas. Una sesión con una conexión abierta NO se considera abandonada aunque no lea (video en
   * pausa con el buffer lleno): quien la reproduce sigue ahí y al dar play debe continuar donde estaba (reiniciar un remux
   * obliga a arrancar en el keyframe anterior). Si el cliente desaparece, su conexión se cierra y entonces cuenta el plazo.
   * Lectura = bytes entregados menos lo que quedó en el buffer: Node llena el buffer por adelantado aunque no haya
   * consumidor, así que contar lo que ffmpeg escribe (o los `_read`) no sirve.
   */
  private detenerInactivas(): void {
    const ahora = Date.now()
    for (const s of [...this.sesiones.values()]) {
      let consumido = 0
      for (const l of s.lectores) consumido = Math.max(consumido, l.pos - l.readableLength)
      if (consumido > s.consumido) {
        s.consumido = consumido
        s.ultimaLectura = ahora
      }
      if (s.lectores.size > 0) {
        s.ultimaLectura = ahora // conectado: el plazo empieza a contar cuando se cierre la última conexión
        continue
      }
      if (ahora - s.ultimaLectura > this.inactividadMs) {
        console.warn(`[reproducción] sesión ${s.id.slice(0, 6)} detenida por inactividad (${Math.round((ahora - s.ultimaLectura) / 1000)} s sin conexión ni lectura)`)
        this.detener(s.id)
      }
    }
  }

  /** Detiene todas las sesiones activas (el servicio sigue operativo). */
  detenerTodas(): void {
    for (const id of [...this.sesiones.keys()]) this.detener(id)
  }

  /** Apagado de la app: detiene las sesiones y el barrido de inactividad. */
  cerrarTodo(): void {
    clearInterval(this.barrido)
    this.detenerTodas()
  }

  // --- subtítulos

  /** Los temporales (subtítulos convertidos) no sobreviven al arranque. */
  private limpiarTemporales(): void {
    try {
      if (existsSync(this.o.carpetaTemporal)) for (const f of readdirSync(this.o.carpetaTemporal)) rmSync(join(this.o.carpetaTemporal, f), { force: true, recursive: true })
      else mkdirSync(this.o.carpetaTemporal, { recursive: true })
    } catch {
      // sin carpeta temporal los subtítulos se convierten en cada petición
    }
  }

  /**
   * Convierte una pista (interna por índice de stream, o un archivo externo) a WebVTT y devuelve la ruta del .vtt.
   * Cacheado en `transcode/` por archivo+pista+tamaño+fecha.
   */
  async subtituloAVtt(origen: { ruta: string; indiceStream: number | null; archivoExterno?: string }): Promise<string> {
    const base = origen.archivoExterno ?? origen.ruta
    const st = statSync(base)
    const clave = createHash('sha1').update(`${base}|${origen.indiceStream ?? 'ext'}|${st.size}|${Math.floor(st.mtimeMs)}`).digest('hex')
    const salida = join(this.o.carpetaTemporal, `${clave}.vtt`)
    if (existsSync(salida)) return salida
    mkdirSync(this.o.carpetaTemporal, { recursive: true })

    let entrada = base
    let intermedio: string | null = null
    if (origen.archivoExterno) {
      // Se normaliza la codificación a UTF-8 antes de pasar por ffmpeg (los .srt viejos suelen ser Windows-1252).
      intermedio = join(this.o.carpetaTemporal, `${clave}${origen.archivoExterno.slice(origen.archivoExterno.lastIndexOf('.'))}`)
      writeFileSync(intermedio, leerTextoSubtitulo(origen.archivoExterno), 'utf8')
      entrada = intermedio
    }
    try {
      await this.ejecutar(this.o.ffmpeg, argsSubtituloAVtt(entrada, origen.archivoExterno ? null : origen.indiceStream, salida), { timeoutMs: 120_000 })
    } finally {
      if (intermedio) rmSync(intermedio, { force: true })
    }
    return salida
  }
}
