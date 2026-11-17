import type { CodigoErrorCliente, CredencialPeticion, EstadoConexion, InfoConexion, PeticionConexion } from '../../shared/cliente-types'
import { EstadoLocal } from './estado-local'
import { Personal } from './personal'
import type { ProxyActivo } from './proxy-cliente'
import { crearProxyCliente, type OpcionesProxy } from './proxy-cliente'
import { huellaCoincide, leerCertificado, parsearUrl, pedirJson, sondear, urlDe, type Destino, type ResultadoSondeo, type SaludRemota } from './remoto'
import type { ServidoresRepo } from './servidores'
import type { TipoCredencial } from '../../shared/cliente-types'
export type { CredencialPeticion, EstadoConexion, InfoConexion, PeticionConexion }

/** Duración de una sesión del servidor (specs/03 §2: «token de sesión (30 días)»). */
const SESION_SEGUNDOS = 30 * 24 * 3600

/** Error con un código que la interfaz traduce a su mensaje/pantalla. `extra` lleva, p. ej., la huella a confirmar. */
export class ErrorCliente extends Error {
  constructor(
    readonly codigo: CodigoErrorCliente,
    mensaje: string,
    readonly extra?: { huella?: string; nombreServidor?: string }
  ) {
    super(mensaje)
  }
}

export interface OpcionesConexion {
  repo: ServidoresRepo
  ahora?: () => number
  crearProxy?: (o: OpcionesProxy) => Promise<ProxyActivo>
}

interface Activa {
  info: InfoConexion
  destino: Destino
  proxy: ProxyActivo
}

/**
 * Conexión de este equipo a otro CinemaLove (Modo Cliente). Se ocupa de verificar el servidor, autenticarse (usuario o
 * token), fijar el certificado autofirmado por huella, recordar el servidor y reconectar. La contraseña nunca se guarda:
 * en modo persistente se guarda el token de sesión; la API key sí se guarda.
 */
export class ConexionCliente {
  private activa: Activa | null = null
  private estadoActual: EstadoConexion = { tipo: 'desconectada' }
  private readonly ahora: () => number
  private readonly crearProxy: (o: OpcionesProxy) => Promise<ProxyActivo>
  private readonly escuchas = new Set<(e: EstadoConexion) => void>()

  constructor(private readonly o: OpcionesConexion) {
    this.ahora = o.ahora ?? ((): number => Math.floor(Date.now() / 1000))
    this.crearProxy = o.crearProxy ?? crearProxyCliente
  }

  estado(): EstadoConexion {
    return this.estadoActual
  }

  /** Avisa cada vez que cambia el estado (p. ej. la sesión venció con la app abierta). Devuelve cómo dejar de escuchar. */
  alCambiar(fn: (e: EstadoConexion) => void): () => void {
    this.escuchas.add(fn)
    return () => this.escuchas.delete(fn)
  }

  private poner(e: EstadoConexion): void {
    this.estadoActual = e
    for (const f of this.escuchas) f(e)
  }

  // ----------------------------------------------------------------- verificación

  /** Paso «Verificar» del asistente: ¿hay un CinemaLove en esa dirección? Usa el certificado ya confiado, si lo hay. */
  async probar(texto: string): Promise<ResultadoSondeo | { tipo: 'url-invalida' }> {
    const destino = parsearUrl(texto)
    if (!destino) return { tipo: 'url-invalida' }
    const guardado = this.o.repo.porDireccion(destino.host, destino.puerto)
    if (destino.https && guardado?.confiarEnCertificado && guardado.certPem) destino.certPem = guardado.certPem
    return sondear(destino)
  }

  private async destinoVerificado(url: string, confiarHuella?: string): Promise<{ destino: Destino; salud: SaludRemota; huella: string | null; pem: string | null }> {
    const s = await this.probar(url)
    switch (s.tipo) {
      case 'url-invalida':
        throw new ErrorCliente('url-invalida', 'La dirección no es válida. Escríbela como http://192.168.0.115:8080.')
      case 'sin-respuesta':
        throw new ErrorCliente('sin-respuesta', 'No hay respuesta en esa dirección. Revisa que el otro equipo esté encendido y comparta su colección.')
      case 'no-cinemalove':
        throw new ErrorCliente('no-cinemalove', 'Hay algo en esa dirección, pero no es un servidor CinemaLove.')
      case 'certificado-no-confiable':
      case 'certificado-cambiado': {
        const nombre = s.tipo === 'certificado-no-confiable' ? s.salud?.nombre : undefined
        if (!confiarHuella || !huellaCoincide(confiarHuella, s.huella)) {
          const msg = s.tipo === 'certificado-cambiado' ? 'El certificado del servidor cambió desde la última vez. Confirma la nueva huella solo si fue el dueño quien lo regeneró.' : 'El servidor usa un certificado propio. Compara la huella con la que muestra en Ajustes → Servidor y confía solo si coincide.'
          throw new ErrorCliente(s.tipo, msg, { huella: s.huella, nombreServidor: nombre })
        }
        // Confirmado por la persona: desde ahora la cadena se valida contra ESE certificado exacto.
        const confiado: Destino = { ...s.destino, certPem: s.pem }
        const r = await sondear(confiado)
        if (r.tipo !== 'ok') throw new ErrorCliente('sin-respuesta', 'No pudimos verificar el servidor con ese certificado.')
        return { destino: confiado, salud: r.salud, huella: s.huella, pem: s.pem }
      }
      case 'ok':
        return { destino: s.destino, salud: s.salud, huella: null, pem: s.destino.certPem ?? null }
    }
  }

  // ----------------------------------------------------------------- conectar

  /** Autentica contra el servidor y devuelve la credencial que se usará (token de sesión o API key). */
  private async autenticar(destino: Destino, c: CredencialPeticion): Promise<{ valor: string; usuario: string | null; expira: number | null }> {
    if (c.tipo === 'sesion') {
      const r = await pedirJson<{ token?: string; nombreUsuario?: string; error?: string }>(destino, { metodo: 'POST', ruta: '/api/v1/login', json: { nombreUsuario: c.usuario, password: c.password } })
      if (r.status === 429) throw new ErrorCliente('demasiados-intentos', 'Demasiados intentos. Espera un minuto e inténtalo de nuevo.')
      if (r.status !== 200 || !r.cuerpo?.token) throw new ErrorCliente('credenciales', 'Usuario o contraseña inválidos.')
      return { valor: r.cuerpo.token, usuario: r.cuerpo.nombreUsuario ?? c.usuario, expira: this.ahora() + SESION_SEGUNDOS }
    }
    const r = await pedirJson<unknown>(destino, { ruta: '/api/v1/me', headers: { 'X-Api-Key': c.apiKey } })
    if (r.status !== 200) throw new ErrorCliente('token-invalido', 'El token de acceso no es válido (¿lo regeneraron en el servidor?).')
    return { valor: c.apiKey, usuario: null, expira: null }
  }

  async conectar(p: PeticionConexion): Promise<EstadoConexion> {
    const { destino, salud, huella, pem } = await this.destinoVerificado(p.url, p.confiarHuella)
    const cred = await this.autenticar(destino, p.credencial)
    const nombre = salud.nombre?.trim() || destino.host

    let servidorId: string | null = null
    if (p.recordar) {
      const previo = this.o.repo.porDireccion(destino.host, destino.puerto)
      const guardado = this.o.repo.guardar({
        host: destino.host,
        puerto: destino.puerto,
        https: destino.https,
        tipoCredencial: p.credencial.tipo,
        apiKey: p.credencial.tipo === 'apiKey' ? p.credencial.apiKey : null,
        ultimoUsuario: cred.usuario,
        tokenSesion: p.credencial.tipo === 'sesion' ? cred.valor : null,
        expiraSesion: cred.expira,
        confiarEnCertificado: !!pem,
        nombre,
        serverUuid: salud.serverId ?? null,
        huella: huella ?? previo?.huella ?? null,
        certPem: pem
      })
      servidorId = guardado.id
    }
    return this.activar({ servidorId, nombre, destino, credencial: p.credencial.tipo, usuario: cred.usuario, valor: cred.valor, recordada: p.recordar })
  }

  private async activar(a: { servidorId: string | null; nombre: string; destino: Destino; credencial: TipoCredencial; usuario: string | null; valor: string; recordada: boolean }): Promise<EstadoConexion> {
    await this.cerrarActiva()
    const info: InfoConexion = { servidorId: a.servidorId, nombre: a.nombre, url: urlDe(a.destino), credencial: a.credencial, usuario: a.usuario, recordada: a.recordada }
    // Con token no hay un usuario en el servidor: el progreso, «Próximas a ver» y las colecciones viven en este equipo.
    // Si el servidor no se recuerda, solo duran lo que dure la sesión.
    const local = a.credencial === 'apiKey' ? (a.servidorId ? this.o.repo.estadoLocal(a.servidorId) : EstadoLocal.enMemoria()) : null
    const proxy = await this.crearProxy({
      destino: a.destino,
      personal: local ? (leer) => new Personal(local, leer) : undefined,
      credencial: () => a.valor,
      nombreServidor: a.nombre,
      alNoAutorizado: () => {
        if (this.activa?.proxy !== proxy) return
        // Una API key regenerada o una sesión revocada: se pide de nuevo la credencial, no se sigue con un proxy inútil.
        if (a.servidorId) this.o.repo.olvidarSesion(a.servidorId)
        void this.cerrarActiva().then(() => this.poner({ tipo: 'sesion-vencida', servidorId: a.servidorId ?? '', nombre: a.nombre, usuario: a.usuario }))
      },
      alFallar: (motivo) => void this.alFallar(a, proxy, motivo)
    })
    this.activa = { info, destino: a.destino, proxy }
    this.poner({ tipo: 'conectada', info, api: { baseUrl: proxy.baseUrl, token: proxy.token } })
    return this.estadoActual
  }

  private comprobandoCaida = false

  /**
   * El proxy no pudo hablar con el servidor. Un fallo suelto (una imagen, un tiempo de espera) no tira la sesión: se confirma
   * con /health. Si de verdad cayó se pasa a «sin conexión»; si el certificado cambió, a «certificado cambiado» (nunca se
   * sigue hablando con un servidor que presenta otra identidad).
   */
  private async alFallar(a: { servidorId: string | null; nombre: string; destino: Destino }, proxy: ProxyActivo, motivo: 'sin-conexion' | 'certificado-cambiado'): Promise<void> {
    if (this.comprobandoCaida || this.activa?.proxy !== proxy) return
    this.comprobandoCaida = true
    try {
      const url = urlDe(a.destino)
      if (motivo === 'certificado-cambiado') {
        const nueva = await leerCertificado(a.destino.host, a.destino.puerto).then((c) => c.huella, () => null)
        const vieja = a.servidorId ? (this.o.repo.obtener(a.servidorId)?.huella ?? '') : ''
        await this.cerrarActiva()
        this.poner({ tipo: 'certificado-cambiado', servidorId: a.servidorId ?? '', nombre: a.nombre, url, huella: vieja, huellaNueva: nueva })
        return
      }
      const vivo = await pedirJson(a.destino, { ruta: '/api/v1/health', timeoutMs: 3000 }).then((r) => r.status === 200, () => false)
      if (vivo || this.activa?.proxy !== proxy) return
      await this.cerrarActiva()
      this.poner({ tipo: 'sin-conexion', servidorId: a.servidorId ?? '', nombre: a.nombre, url })
    } finally {
      this.comprobandoCaida = false
    }
  }

  // ----------------------------------------------------------------- reconectar

  /** Reconexión automática al abrir la app (Modo Cliente persistente). Nunca cae a Modo Local en silencio. */
  async reconectar(servidorId: string): Promise<EstadoConexion> {
    const s = this.o.repo.obtener(servidorId)
    if (!s) throw new ErrorCliente('servidor-desconocido', 'Este servidor ya no está guardado.')
    const nombre = s.nombre ?? s.host
    const destino: Destino = { host: s.host, puerto: s.puerto, https: s.https, certPem: s.confiarEnCertificado ? s.certPem : null }
    const url = urlDe(destino)

    const esSesion = s.tipoCredencial === 'sesion'
    if (esSesion && (!s.tokenSesion || (s.expiraSesion ?? 0) <= this.ahora())) {
      this.poner({ tipo: 'sesion-vencida', servidorId, nombre, usuario: s.ultimoUsuario })
      return this.estadoActual
    }
    const valor = esSesion ? s.tokenSesion! : s.apiKey
    if (!valor) {
      this.poner({ tipo: 'sesion-vencida', servidorId, nombre, usuario: s.ultimoUsuario })
      return this.estadoActual
    }

    let status: number
    try {
      status = (await pedirJson<unknown>(destino, { ruta: '/api/v1/me', headers: { 'X-Api-Key': valor }, timeoutMs: 6000 })).status
    } catch (e) {
      const cert = destino.https && /CERT|SELF_SIGNED|UNABLE_TO_VERIFY|ERR_TLS/i.test(String((e as NodeJS.ErrnoException).code ?? '') + String((e as Error).message))
      if (cert) {
        // La huella que presenta AHORA, para que la persona la compare con la que ve el dueño en Ajustes → Servidor.
        const nueva = await leerCertificado(destino.host, destino.puerto).then((c) => c.huella, () => null)
        this.poner({ tipo: 'certificado-cambiado', servidorId, nombre, url, huella: s.huella ?? '', huellaNueva: nueva })
      } else this.poner({ tipo: 'sin-conexion', servidorId, nombre, url })
      return this.estadoActual
    }
    if (status === 401) {
      this.o.repo.olvidarSesion(servidorId)
      this.poner({ tipo: 'sesion-vencida', servidorId, nombre, usuario: s.ultimoUsuario })
      return this.estadoActual
    }
    if (status !== 200) {
      this.poner({ tipo: 'sin-conexion', servidorId, nombre, url })
      return this.estadoActual
    }
    this.o.repo.guardar({ host: s.host, puerto: s.puerto, https: s.https })
    return this.activar({ servidorId, nombre, destino, credencial: s.tipoCredencial, usuario: s.ultimoUsuario, valor, recordada: true })
  }

  /** Tras «Tu sesión venció»: se pide la contraseña de nuevo y se renueva el token. */
  async renovarSesion(servidorId: string, password: string): Promise<EstadoConexion> {
    const s = this.o.repo.obtener(servidorId)
    if (!s || !s.ultimoUsuario) throw new ErrorCliente('servidor-desconocido', 'Este servidor ya no está guardado.')
    const destino: Destino = { host: s.host, puerto: s.puerto, https: s.https, certPem: s.confiarEnCertificado ? s.certPem : null }
    let cred
    try {
      cred = await this.autenticar(destino, { tipo: 'sesion', usuario: s.ultimoUsuario, password })
    } catch (e) {
      if (e instanceof ErrorCliente) throw e
      throw new ErrorCliente('sin-conexion', `No pudimos conectar con «${s.nombre ?? s.host}».`)
    }
    this.o.repo.guardar({ host: s.host, puerto: s.puerto, https: s.https, tokenSesion: cred.valor, expiraSesion: cred.expira, ultimoUsuario: cred.usuario })
    return this.activar({ servidorId, nombre: s.nombre ?? s.host, destino, credencial: 'sesion', usuario: cred.usuario, valor: cred.valor, recordada: true })
  }

  /** Tras un certificado cambiado: la persona confirmó la huella nueva. */
  async confiarCertificadoNuevo(servidorId: string, huella: string): Promise<EstadoConexion> {
    const s = this.o.repo.obtener(servidorId)
    if (!s) throw new ErrorCliente('servidor-desconocido', 'Este servidor ya no está guardado.')
    const sondeo = await sondear({ host: s.host, puerto: s.puerto, https: s.https })
    if (sondeo.tipo !== 'certificado-no-confiable' || !huellaCoincide(huella, sondeo.huella)) throw new ErrorCliente('certificado-cambiado', 'La huella no coincide con el certificado que presenta el servidor.', { huella: sondeo.tipo === 'certificado-no-confiable' ? sondeo.huella : undefined })
    this.o.repo.guardar({ host: s.host, puerto: s.puerto, https: s.https, confiarEnCertificado: true, huella: sondeo.huella, certPem: sondeo.pem })
    return this.reconectar(servidorId)
  }

  // ----------------------------------------------------------------- salir

  private async cerrarActiva(): Promise<void> {
    const a = this.activa
    this.activa = null
    await a?.proxy.cerrar().catch(() => undefined)
  }

  /** Vuelve a «sin conexión remota» (p. ej. a Modo Local). No borra lo recordado. */
  async desconectar(): Promise<void> {
    await this.cerrarActiva()
    this.poner({ tipo: 'desconectada' })
  }

  /** «Olvidar este servidor»: borra lo recordado de él (token, certificado confiado, progreso local). */
  async olvidar(servidorId: string): Promise<void> {
    if (this.activa?.info.servidorId === servidorId) await this.desconectar()
    this.o.repo.eliminar(servidorId)
    if (this.estadoActual.tipo !== 'conectada') this.poner({ tipo: 'desconectada' })
  }

  async cerrar(): Promise<void> {
    await this.cerrarActiva()
    this.escuchas.clear()
  }
}
