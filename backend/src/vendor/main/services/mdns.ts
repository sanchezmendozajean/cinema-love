import { Bonjour, type Browser, type Service } from 'bonjour-service'

/** Tipo de servicio anunciado: `_cinemalove._tcp` (specs/03 §4). */
const TIPO = 'cinemalove'

export interface AnuncioMdns {
  nombre: string
  puerto: number
  /** `v` versión de API, `id` = `cl_meta.server_id`, `name`, `tls`. */
  txt: Record<string, string>
}

export interface ServidorDescubierto {
  /** `id` del TXT (deduplicación). */
  id: string
  nombre: string
  host: string
  puerto: number
  tls: boolean
}

/** Lo mínimo que el servidor público necesita de mDNS (inyectable en las pruebas). */
export interface Publicador {
  publicar(a: AnuncioMdns): void
  detener(): Promise<void>
}

/** Anuncio y descubrimiento en la LAN con `bonjour-service`. La instancia se crea la primera vez que se usa. */
export class Mdns implements Publicador {
  private bonjour: Bonjour | null = null
  private servicio: Service | null = null
  private busqueda: Browser | null = null

  private instancia(): Bonjour {
    this.bonjour ??= new Bonjour()
    return this.bonjour
  }

  publicar(a: AnuncioMdns): void {
    this.servicio?.stop?.()
    this.servicio = this.instancia().publish({ name: a.nombre, type: TIPO, protocol: 'tcp', port: a.puerto, txt: a.txt })
  }

  /** Busca servidores CinemaLove. Devuelve la función que detiene la búsqueda. */
  descubrir(alEncontrar: (s: ServidorDescubierto) => void, alPerder: (id: string) => void): () => void {
    this.busqueda?.stop()
    const tomar = (s: Service): ServidorDescubierto | null => {
      const txt = (s.txt ?? {}) as Record<string, unknown>
      const id = typeof txt.id === 'string' ? txt.id : null
      if (!id) return null
      // Se prefiere IPv4: las direcciones IPv6 link-local no sirven en una URL sin zona.
      const v4 = (s.addresses ?? []).find((d) => /^\d+\.\d+\.\d+\.\d+$/.test(d))
      return { id, nombre: typeof txt.name === 'string' ? txt.name : s.name, host: v4 ?? s.host, puerto: s.port, tls: txt.tls === '1' }
    }
    this.busqueda = this.instancia().find({ type: TIPO, protocol: 'tcp' })
    this.busqueda.on('up', (s: Service) => {
      const d = tomar(s)
      if (d) alEncontrar(d)
    })
    this.busqueda.on('down', (s: Service) => {
      const d = tomar(s)
      if (d) alPerder(d.id)
    })
    return () => {
      this.busqueda?.stop()
      this.busqueda = null
    }
  }

  async detener(): Promise<void> {
    this.servicio?.stop?.()
    this.servicio = null
    this.busqueda?.stop()
    this.busqueda = null
    const b = this.bonjour
    this.bonjour = null
    if (b) await new Promise<void>((r) => b.destroy(() => r()))
  }
}
