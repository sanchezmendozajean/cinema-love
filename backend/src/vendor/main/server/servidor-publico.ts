import { createServer } from 'node:net'
import { hostname, networkInterfaces } from 'node:os'
import type { AddressInfo } from 'node:net'
import type Database from 'better-sqlite3'
import type { FastifyInstance } from 'fastify'
import { AuthRepo } from '../repositories/auth'
import type { Publicador } from '../services/mdns'
import { AlmacenCertificados, type Certificado } from './certs'

export type Protocolo = 'http' | 'https'

export interface ConfigServidor {
  protocolo: Protocolo
  puertoHttp: number
  puertoHttps: number
  nombre: string
  /** Anunciar en la red por mDNS. */
  mdns: boolean
  /** Arrancar el servidor al abrir CinemaLove. */
  autostart: boolean
}

export interface EstadoServidor {
  activo: boolean
  /** Protocolo con el que está corriendo (o con el que arrancaría). */
  protocolo: Protocolo
  puerto: number
  nombre: string
  /** IP de la LAN de este equipo. */
  direcciones: string[]
  /** URL completas para conectarse (una por IP). */
  urls: string[]
  /** Huella SHA-256 del certificado, si ya existe uno. */
  huella: string | null
  anunciando: boolean
}

export class ErrorServidor extends Error {
  constructor(
    mensaje: string,
    readonly status = 400
  ) {
    super(mensaje)
  }
}

// Claves heredadas de Flutter (`server_*`) y propias (`cl.*`) de `app_settings` (specs/02 §5).
const K = { modo: 'server_mode', http: 'server_http_port', https: 'server_https_port', nombre: 'cl.server_name', auto: 'cl.server_autostart', mdns: 'cl.discovery_enabled' } as const
const PUERTO_MIN = 1024

/** IPv4 de las interfaces de red con las privadas (192.168/10/172.16) primero. */
export function direccionesLan(): string[] {
  const ips: string[] = []
  for (const lista of Object.values(networkInterfaces())) {
    for (const i of lista ?? []) if (i.family === 'IPv4' && !i.internal) ips.push(i.address)
  }
  const privada = (ip: string): number => (/^(192\.168\.|10\.|172\.(1[6-9]|2\d|3[01])\.)/.test(ip) ? 0 : 1)
  return ips.sort((a, b) => privada(a) - privada(b))
}

/** ¿Se puede escuchar en este puerto en todas las interfaces? */
export function puertoLibre(puerto: number): Promise<boolean> {
  return new Promise((resolve) => {
    const s = createServer()
    s.once('error', () => resolve(false))
    s.listen({ host: '0.0.0.0', port: puerto, exclusive: true }, () => s.close(() => resolve(true)))
  })
}

export interface OpcionesServidor {
  db: Database.Database
  carpetaDatos: string
  /** Crea la app Fastify del listener público (sin `adminToken`: no expone administración). */
  crearApp: (extra: { https?: { key: string; cert: string } }) => Promise<FastifyInstance>
  mdns?: Publicador
  direcciones?: () => string[]
  nombreEquipo?: () => string
}

/**
 * Listener público (specs/03 §4): HTTP **o** HTTPS en `0.0.0.0`, anunciado por mDNS. Se inicia y detiene sin reiniciar la
 * app. El protocolo y el puerto no se pueden cambiar con el servidor corriendo (como en Flutter).
 */
export class ServidorPublico {
  private app: FastifyInstance | null = null
  private corriendoEn: { protocolo: Protocolo; puerto: number } | null = null
  private anunciando = false
  private readonly ajustes: AuthRepo
  private readonly certs: AlmacenCertificados
  private readonly ips: () => string[]

  constructor(private readonly o: OpcionesServidor) {
    this.ajustes = new AuthRepo(o.db)
    this.certs = new AlmacenCertificados(o.carpetaDatos)
    this.ips = o.direcciones ?? direccionesLan
  }

  get activo(): boolean {
    return this.app !== null
  }

  config(): ConfigServidor {
    const entero = (k: string, def: number): number => {
      const n = Number.parseInt(this.ajustes.ajuste(k) ?? '', 10)
      return Number.isInteger(n) && n >= 0 && n <= 65535 ? n : def
    }
    return {
      protocolo: this.ajustes.ajuste(K.modo) === 'https' ? 'https' : 'http',
      puertoHttp: entero(K.http, 8080),
      puertoHttps: entero(K.https, 8443),
      nombre: this.ajustes.ajuste(K.nombre)?.trim() || (this.o.nombreEquipo ?? hostname)(),
      mdns: this.ajustes.ajuste(K.mdns) !== 'false',
      autostart: this.ajustes.ajuste(K.auto) === 'true'
    }
  }

  nombre(): string {
    return this.config().nombre
  }

  estado(): EstadoServidor {
    const c = this.config()
    const protocolo = this.corriendoEn?.protocolo ?? c.protocolo
    const puerto = this.corriendoEn?.puerto ?? (protocolo === 'https' ? c.puertoHttps : c.puertoHttp)
    const direcciones = this.ips()
    return {
      activo: this.activo,
      protocolo,
      puerto,
      nombre: c.nombre,
      direcciones,
      urls: direcciones.map((ip) => `${protocolo}://${ip}:${puerto}`),
      huella: this.certs.cargar()?.huella ?? null,
      anunciando: this.anunciando
    }
  }

  /** Guarda la configuración. Protocolo y puertos solo con el servidor detenido; el puerto debe estar libre. */
  async guardarConfig(c: Partial<ConfigServidor>): Promise<ConfigServidor> {
    const actual = this.config()
    const cambiaRed = (c.protocolo !== undefined && c.protocolo !== actual.protocolo) || (c.puertoHttp !== undefined && c.puertoHttp !== actual.puertoHttp) || (c.puertoHttps !== undefined && c.puertoHttps !== actual.puertoHttps)
    if (this.activo && cambiaRed) throw new ErrorServidor('Detén el servidor para cambiar el protocolo o el puerto.', 409)
    if (c.protocolo !== undefined && c.protocolo !== 'http' && c.protocolo !== 'https') throw new ErrorServidor('Protocolo inválido.')
    for (const [clave, valor] of [['HTTP', c.puertoHttp], ['HTTPS', c.puertoHttps]] as const) {
      if (valor === undefined) continue
      if (!Number.isInteger(valor) || valor < PUERTO_MIN || valor > 65535) throw new ErrorServidor(`El puerto ${clave} debe estar entre ${PUERTO_MIN} y 65535.`)
      const nuevo = valor !== (clave === 'HTTP' ? actual.puertoHttp : actual.puertoHttps)
      if (nuevo && !(await puertoLibre(valor))) throw new ErrorServidor(`El puerto ${valor} ya está en uso por otro programa.`, 409)
    }
    if (c.nombre !== undefined && (typeof c.nombre !== 'string' || c.nombre.trim() === '' || c.nombre.trim().length > 60)) throw new ErrorServidor('El nombre del servidor debe tener entre 1 y 60 caracteres.')

    if (c.protocolo !== undefined) this.ajustes.guardarAjuste(K.modo, c.protocolo)
    if (c.puertoHttp !== undefined) this.ajustes.guardarAjuste(K.http, String(c.puertoHttp))
    if (c.puertoHttps !== undefined) this.ajustes.guardarAjuste(K.https, String(c.puertoHttps))
    if (c.nombre !== undefined) this.ajustes.guardarAjuste(K.nombre, c.nombre.trim())
    if (c.mdns !== undefined) this.ajustes.guardarAjuste(K.mdns, String(c.mdns))
    if (c.autostart !== undefined) this.ajustes.guardarAjuste(K.auto, String(c.autostart))

    // Nombre y mDNS sí pueden cambiar en caliente.
    if (this.activo && this.corriendoEn) this.actualizarAnuncio()
    return this.config()
  }

  private actualizarAnuncio(): void {
    const c = this.config()
    if (!this.corriendoEn) return
    if (c.mdns && this.o.mdns) {
      this.o.mdns.publicar({
        nombre: c.nombre,
        puerto: this.corriendoEn.puerto,
        txt: { v: '1', id: this.ajustes.serverId(), name: c.nombre, tls: this.corriendoEn.protocolo === 'https' ? '1' : '0' }
      })
      this.anunciando = true
    } else {
      this.anunciando = false
      void this.o.mdns?.detener()
    }
  }

  async iniciar(): Promise<EstadoServidor> {
    if (this.app) return this.estado()
    const c = this.config()
    const puerto = c.protocolo === 'https' ? c.puertoHttps : c.puertoHttp
    let cert: Certificado | undefined
    if (c.protocolo === 'https') cert = this.certs.cargarOGenerar({ nombre: c.nombre, ips: this.ips() })
    const app = await this.o.crearApp({ https: cert ? { key: cert.key, cert: cert.cert } : undefined })
    try {
      await app.listen({ host: '0.0.0.0', port: puerto })
    } catch (e) {
      await app.close().catch(() => undefined)
      const codigo = (e as NodeJS.ErrnoException).code
      if (codigo === 'EADDRINUSE') throw new ErrorServidor(`El puerto ${puerto} ya está en uso por otro programa. Elige otro en Ajustes → Servidor.`, 409)
      if (codigo === 'EACCES') throw new ErrorServidor(`No hay permiso para usar el puerto ${puerto}. Elige uno mayor a 1024.`, 409)
      throw e
    }
    this.app = app
    this.corriendoEn = { protocolo: c.protocolo, puerto: (app.server.address() as AddressInfo).port }
    this.actualizarAnuncio()
    return this.estado()
  }

  async detener(): Promise<EstadoServidor> {
    const app = this.app
    this.app = null
    this.corriendoEn = null
    this.anunciando = false
    await this.o.mdns?.detener().catch(() => undefined)
    if (app) await app.close()
    return this.estado()
  }

  /** Genera un certificado nuevo. Si el servidor estaba corriendo en HTTPS se reinicia para usarlo. */
  async regenerarCertificado(): Promise<EstadoServidor> {
    const reiniciar = this.activo && this.corriendoEn?.protocolo === 'https'
    if (reiniciar) await this.detener()
    this.certs.regenerar({ nombre: this.config().nombre, ips: this.ips() })
    if (reiniciar) await this.iniciar()
    return this.estado()
  }
}
