import { app } from 'electron'
import { join, resolve } from 'node:path'
import { abrirBaseCliente, type BaseCliente } from './db/cliente-database'
import { ConexionCliente, ErrorCliente } from './client/conexion-cliente'
import { urlDe } from './client/remoto'
import { ServidoresRepo } from './client/servidores'
import { ConfigService, carpetaDeConfig } from './config/config-service'
import { ErrorApertura, abrirBase, type BaseAbierta } from './db/database'
import { Repositorios } from './repositories/repositorios'
import { RegistroActividad } from './server/actividad'
import { crearApp } from './server/app'
import { iniciarListenerInterno, type ListenerInterno } from './server/listener-interno'
import { ServidorPublico } from './server/servidor-publico'
import { Mdns } from './services/mdns'
import { FlutterImageCache } from './services/flutter-image-cache'
import { ImageService } from './services/image-service'
import { resolverFfmpeg } from './services/ffmpeg-paths'
import { MediaProbeService } from './services/media-probe'
import { PlaybackService } from './services/playback-service'
import { AuthRepo } from './repositories/auth'
import type { AceleracionHw } from './services/ffmpeg-args'
import type { EstadoConexion, PeticionConexion, RespuestaConexion, SondeoCliente } from '../shared/cliente-types'
import type { EstadoDatos } from '../shared/ipc'

/** Raíz de servicios del proceso main: configuración, colección local, servidor público y conexión a otro CinemaLove. */
export class AppCore {
  private base: BaseAbierta | null = null
  repos: Repositorios | null = null
  private interno: ListenerInterno | null = null
  private playback: PlaybackService | null = null
  private servidor: ServidorPublico | null = null
  private baseCliente: BaseCliente | null = null
  private conexion: ConexionCliente | null = null
  private escuchasConexion = new Set<(e: EstadoConexion) => void>()
  readonly mdns = new Mdns()
  private carpetaAbierta: string | null = null

  /** Carpeta de datos de la colección local abierta (null si no hay). */
  get carpetaDatos(): string | null {
    return this.base ? this.carpetaAbierta : null
  }

  /** «IP:puerto» si este equipo está compartiendo su colección (se indica también mientras ve la de otro). */
  get compartiendo(): string | null {
    const e = this.servidor?.estado()
    return e?.activo ? `${e.direcciones[0] ?? 'este equipo'}:${e.puerto}` : null
  }
  config!: ConfigService
  estadoDatos: EstadoDatos = { ok: false, motivo: 'sin-configurar', mensaje: 'Aún no hay carpeta de datos.' }

  /** Solo en desarrollo: las pruebas e2e trabajan sobre una copia temporal de los datos. */
  private get carpetaDev(): string | null {
    return (!app.isPackaged && process.env.CL_DATA_DIR) || null
  }

  /** Estado de la conexión remota (`desconectada` si se usa la colección local). */
  get estadoConexion(): EstadoConexion {
    return this.conexion?.estado() ?? { tipo: 'desconectada' }
  }

  /** API que usa el renderer: el proxy al servidor remoto si hay una conexión activa; si no, el listener interno. */
  get api(): { baseUrl: string; token: string } | null {
    const c = this.estadoConexion
    if (c.tipo === 'conectada') return c.api
    // Con una conexión remota pendiente (sin conexión, sesión vencida…) NO se cae a la colección local en silencio.
    if (c.tipo !== 'desconectada') return null
    return this.interno ? { baseUrl: this.interno.baseUrl, token: this.interno.token } : null
  }

  get modo(): 'local' | 'cliente' {
    return this.estadoConexion.tipo === 'desconectada' ? 'local' : 'cliente'
  }

  async iniciar(): Promise<void> {
    this.config = new ConfigService(
      carpetaDeConfig({
        empaquetada: app.isPackaged,
        execPath: process.execPath,
        env: process.env,
        devDataDir: join(app.getAppPath(), '.dev-data'),
        userData: app.getPath('userData')
      })
    )
    const cfg = this.config.leer()
    if (!this.carpetaDev && cfg.modo === 'cliente') return this.iniciarCliente(cfg.servidorId)
    const carpeta = this.carpetaDev || this.config.carpetaDeDatos()
    if (!carpeta) return
    await this.abrirColeccion(carpeta)
  }

  // ------------------------------------------------------------------ colección local

  /** Abre la colección local de `carpeta` y levanta el listener interno (y el servidor público si hay autostart). */
  async abrirColeccion(carpeta: string): Promise<void> {
    await this.cerrarColeccion()
    try {
      this.base = abrirBase(carpeta)
      this.carpetaAbierta = carpeta
      this.repos = new Repositorios(this.base.db)
      const carpetaImages = join(carpeta, 'images')
      // ffmpeg/ffprobe empaquetados (nunca los del sistema). Sin ellos solo funciona el modo direct.
      // `CL_SIN_FFMPEG=1` (solo en desarrollo) simula una instalación sin ffmpeg: queda únicamente el modo direct.
      const sinFfmpeg = !app.isPackaged && process.env.CL_SIN_FFMPEG === '1'
      const ffmpeg = sinFfmpeg ? null : resolverFfmpeg({ empaquetada: app.isPackaged, resourcesPath: process.resourcesPath, raizProyecto: app.getAppPath() })
      const ajustes = new AuthRepo(this.base.db)
      if (ffmpeg) {
        this.playback = new PlaybackService({
          ffmpeg: ffmpeg.ffmpeg,
          carpetaTemporal: join(carpeta, 'transcode'),
          ajustes: () => ({
            hw: (ajustes.ajuste('cl.hw_accel') ?? 'off') as AceleracionHw,
            maxTranscodes: Math.max(1, Number(ajustes.ajuste('cl.max_transcodes')) || 2)
          })
        })
      }
      const images = new ImageService(carpetaImages, new FlutterImageCache(carpetaImages), this.base.soloLectura ? null : this.base.db)
      const reproduccion = ffmpeg && this.playback ? { probe: new MediaProbeService(ffmpeg.ffprobe, this.base.db), playback: this.playback } : undefined
      const db = this.base.db
      const soloLectura = this.base.soloLectura
      const actividad = new RegistroActividad()
      // Servidor público: otro listener sobre los mismos servicios, sin administración (specs/01 §2).
      const servidor: ServidorPublico = new ServidorPublico({
        db,
        carpetaDatos: carpeta,
        mdns: this.mdns,
        crearApp: (extra) => crearApp({ db, images, soloLectura, reproduccion, actividad, nombreServidor: (): string => servidor.nombre(), https: extra.https })
      })
      this.servidor = servidor
      this.interno = await iniciarListenerInterno({ carpetaDatos: carpeta, reproduccion, db, soloLectura, images, actividad, servidor, nombreServidor: () => servidor.nombre() })
      if (servidor.config().autostart) void servidor.iniciar().catch((e: unknown) => console.error('No se pudo iniciar el servidor automáticamente:', e instanceof Error ? e.message : e))
      const c = this.repos.contarItems()
      this.estadoDatos = {
        ok: true,
        soloLectura: this.base.soloLectura,
        aviso: this.base.aviso,
        bibliotecas: this.repos.listarBibliotecas().length,
        peliculas: c.peliculas,
        series: this.repos.listarSeries().length,
        episodios: c.episodios
      }
    } catch (e) {
      await this.cerrarColeccion()
      this.estadoDatos = {
        ok: false,
        motivo: e instanceof ErrorApertura ? e.codigo : 'error',
        mensaje: e instanceof Error ? e.message : String(e)
      }
    }
  }

  private async cerrarColeccion(): Promise<void> {
    this.playback?.cerrarTodo()
    this.playback = null
    await this.servidor?.detener().catch(() => undefined)
    this.servidor = null
    await this.interno?.cerrar()
    this.interno = null
    this.base?.cerrar()
    this.base = null
    this.repos = null
  }

  /** Asistente, camino A: guarda `config.yaml` con la carpeta de datos y abre la colección. */
  async aplicarCarpetaDeDatos(carpeta: string): Promise<EstadoDatos> {
    this.config.guardar({ modo: 'local', databaseDirectory: carpeta, servidorId: null })
    await this.abrirColeccion(carpeta)
    return this.estadoDatos
  }

  /**
   * Ajustes → Datos → «Cambiar carpeta»: pasa a usar OTRA carpeta de datos. No borra ni mueve nada de la anterior. Si la nueva
   * no se puede abrir (base de una versión más nueva, bloqueada, sin permiso…), se restaura `config.yaml` y se vuelve a abrir
   * la colección anterior: la persona nunca se queda sin colección ni con la configuración rota. En ese caso se devuelve el
   * error de la carpeta nueva (`ok: false`) y la colección anterior sigue abierta.
   */
  async cambiarCarpetaDeDatos(carpeta: string): Promise<EstadoDatos & { restaurada?: boolean }> {
    const anterior = this.carpetaDatos
    if (anterior && resolve(anterior).toLowerCase() === resolve(carpeta).toLowerCase()) return this.estadoDatos
    const previo = this.config.leer()
    this.config.guardar({ modo: 'local', databaseDirectory: carpeta, servidorId: null })
    await this.abrirColeccion(carpeta)
    if (this.estadoDatos.ok || !anterior) return this.estadoDatos
    const fallo = this.estadoDatos
    this.config.guardar(previo)
    await this.abrirColeccion(anterior)
    return { ...fallo, restaurada: this.estadoDatos.ok }
  }

  // ------------------------------------------------------------------ Modo Cliente

  /** Carpeta de `cliente_datos.sqlite`: junto a `config.yaml` (en dev con `CL_DATA_DIR`, esa carpeta de pruebas). */
  private get carpetaCliente(): string {
    return this.carpetaDev ?? this.config.carpeta
  }

  /** La conexión remota, creada la primera vez que se necesita (abre `cliente_datos.sqlite`). */
  conexionCliente(): ConexionCliente {
    if (!this.conexion) {
      this.baseCliente = abrirBaseCliente(this.carpetaCliente)
      this.conexion = new ConexionCliente({ repo: new ServidoresRepo(this.baseCliente.db) })
      this.conexion.alCambiar((e) => {
        for (const f of this.escuchasConexion) f(e)
      })
    }
    return this.conexion
  }

  /** Avisa cuando cambia el estado de la conexión (p. ej. la sesión venció con la app abierta). */
  alCambiarConexion(fn: (e: EstadoConexion) => void): () => void {
    this.escuchasConexion.add(fn)
    return () => this.escuchasConexion.delete(fn)
  }

  /** Arranque en Modo Cliente persistente: se reconecta al servidor recordado. Nunca cae a Modo Local en silencio. */
  private async iniciarCliente(servidorId: string | null): Promise<void> {
    if (!servidorId) {
      this.estadoDatos = { ok: false, motivo: 'error', mensaje: 'La configuración indica Modo Cliente pero no hay un servidor guardado. Reconfigura CinemaLove.' }
      return
    }
    try {
      await this.conexionCliente().reconectar(servidorId)
    } catch (e) {
      this.estadoDatos = { ok: false, motivo: 'error', mensaje: e instanceof Error ? e.message : String(e) }
    }
  }

  private respuesta = async (fn: () => Promise<EstadoConexion>): Promise<RespuestaConexion> => {
    try {
      return { ok: true, estado: await fn() }
    } catch (e) {
      if (e instanceof ErrorCliente) return { ok: false, codigo: e.codigo, mensaje: e.message, huella: e.extra?.huella, nombreServidor: e.extra?.nombreServidor }
      throw e
    }
  }

  /** Paso «Verificar» del asistente, ya en la forma que muestra la interfaz. */
  async probarServidor(url: string): Promise<SondeoCliente> {
    const s = await this.conexionCliente().probar(url)
    switch (s.tipo) {
      case 'ok':
        return { tipo: 'ok', nombre: s.salud.nombre ?? s.destino.host, url: urlDe(s.destino), https: s.destino.https }
      case 'certificado-no-confiable':
        return { tipo: 'certificado-no-confiable', nombre: s.salud?.nombre ?? null, url: urlDe(s.destino), huella: s.huella }
      case 'certificado-cambiado':
        return { tipo: 'certificado-cambiado', nombre: null, url: urlDe(s.destino), huella: s.huella }
      default:
        return { tipo: s.tipo }
    }
  }

  async conectar(p: PeticionConexion, configurar: boolean): Promise<RespuestaConexion> {
    const r = await this.respuesta(() => this.conexionCliente().conectar(p))
    // Modo Cliente persistente: solo el asistente deja la conexión escrita en config.yaml.
    if (r.ok && configurar && r.estado.tipo === 'conectada' && r.estado.info.servidorId) {
      this.config.guardar({ modo: 'cliente', servidorId: r.estado.info.servidorId, databaseDirectory: null })
    }
    return r
  }

  renovarSesion = (password: string): Promise<RespuestaConexion> => {
    const e = this.estadoConexion
    if (e.tipo !== 'sesion-vencida') return Promise.resolve({ ok: false, codigo: 'servidor-desconocido', mensaje: 'No hay una sesión que renovar.' })
    return this.respuesta(() => this.conexionCliente().renovarSesion(e.servidorId, password))
  }

  confiarCertificadoNuevo = (huella: string): Promise<RespuestaConexion> => {
    const e = this.estadoConexion
    if (e.tipo !== 'certificado-cambiado') return Promise.resolve({ ok: false, codigo: 'servidor-desconocido', mensaje: 'No hay un certificado pendiente de confirmar.' })
    return this.respuesta(() => this.conexionCliente().confiarCertificadoNuevo(e.servidorId, huella))
  }

  /** «Reintentar» en la pantalla de sin conexión. */
  reintentarConexion(): Promise<EstadoConexion> {
    const e = this.estadoConexion
    const id = e.tipo === 'conectada' ? e.info.servidorId : e.tipo === 'desconectada' ? null : e.servidorId
    if (!id) return Promise.resolve(e)
    return this.conexionCliente().reconectar(id)
  }

  /** «Olvidar este servidor»: borra lo recordado y, si config.yaml lo describía, también config.yaml (→ asistente). */
  async olvidarServidor(): Promise<void> {
    const e = this.estadoConexion
    const id = e.tipo === 'conectada' ? e.info.servidorId : e.tipo === 'desconectada' ? null : e.servidorId
    if (!id) return
    await this.conexionCliente().olvidar(id)
    if (this.config.leer().servidorId === id) this.config.borrar()
    this.estadoDatos = { ok: false, motivo: 'sin-configurar', mensaje: 'Aún no hay carpeta de datos.' }
  }

  async desconectar(): Promise<void> {
    await this.conexion?.desconectar()
  }

  async cerrar(): Promise<void> {
    await this.conexion?.cerrar()
    this.conexion = null
    this.baseCliente?.cerrar()
    this.baseCliente = null
    await this.cerrarColeccion()
    await this.mdns.detener().catch(() => undefined)
  }
}
