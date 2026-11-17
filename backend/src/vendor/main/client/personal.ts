import { ESTADOS_VISTO, type EstadoVisto } from '../../shared/enums'
import type { Coleccion, ElementoCatalogo, Home, Progreso } from '../../shared/api-types'
import type { ColeccionLocal, EstadoLocal, ProgresoLocal } from './estado-local'

export interface PeticionPersonal {
  metodo: string
  /** Ruta bajo `/api/v1`, sin query: `/media/abc/progress`. */
  ruta: string
  query: URLSearchParams
  /** Cuerpo JSON ya interpretado (o `null`). */
  cuerpo: unknown
}

export interface RespuestaPersonal {
  status: number
  cuerpo: unknown
}

/** Pide JSON al servidor remoto (con su credencial). `null` si no se pudo. */
export type LectorRemoto = (ruta: string, cuerpo?: unknown) => Promise<unknown | null>

const error = (status: number, mensaje: string): RespuestaPersonal => ({ status, cuerpo: { error: mensaje } })
const ok = (cuerpo: unknown): RespuestaPersonal => ({ status: 200, cuerpo })

const progresoJson = (p: ProgresoLocal | null): Progreso =>
  p
    ? { posicionSegundos: p.posicionSegundos, estado: p.estado, ...(p.pistaAudioId ? { pistaAudioId: p.pistaAudioId } : {}), ...(p.pistaSubtituloId ? { pistaSubtituloId: p.pistaSubtituloId } : {}) }
    : { posicionSegundos: 0, estado: 'noVisto' }

const resumen = (p: ProgresoLocal | null): { estado: EstadoVisto; posicionSegundos: number } | null =>
  p && p.estado !== 'noVisto' ? { estado: p.estado, posicionSegundos: p.posicionSegundos } : null

interface PutProgreso {
  estado: EstadoVisto | null
  pos: number
  audio: string | null
  sub: string | null
}

/** Valida el cuerpo de un PUT de progreso, como el servidor (`routes/usuario.ts`). */
function leerProgreso(cuerpo: unknown): PutProgreso | string {
  const b = (cuerpo ?? {}) as Record<string, unknown>
  if (b.estado !== undefined && b.estado !== null && !(ESTADOS_VISTO as readonly unknown[]).includes(b.estado)) return `estado inválido: ${String(b.estado)}`
  return {
    estado: (b.estado as EstadoVisto | undefined) ?? null,
    pos: typeof b.posicionSegundos === 'number' && Number.isFinite(b.posicionSegundos) ? Math.max(0, Math.trunc(b.posicionSegundos)) : 0,
    audio: typeof b.pistaAudioId === 'string' ? b.pistaAudioId : null,
    sub: typeof b.pistaSubtituloId === 'string' ? b.pistaSubtituloId : null
  }
}

const esArreglo = (x: unknown): x is unknown[] => Array.isArray(x)
const quitarVacios = (ids: string[]): string[] => ids.filter(Boolean)

/**
 * Lo personal del Modo Cliente con token: el servidor remoto pone el catálogo y ESTE equipo guarda el progreso, «Próximas a
 * ver» y las colecciones (specs/03 §5). Responde en el mismo formato que las rutas de `routes/usuario.ts` del servidor, para que
 * el renderer no note la diferencia. Lo que no se puede resolver aquí devuelve `null` y el proxy lo reenvía al servidor.
 */
export class Personal {
  constructor(
    private readonly local: EstadoLocal,
    private readonly remoto: LectorRemoto
  ) {}

  /**
   * ¿Esta ruta se atiende aquí (`local`), se le pide al servidor con el progreso de este equipo (`consulta`), se reenvía y se
   * corrige después (`superponer`) o va tal cual (`null`)?
   */
  clasificar(metodo: string, ruta: string): 'local' | 'consulta' | 'superponer' | null {
    if (metodo === 'GET' && (ruta === '/home' || ruta === '/catalog')) return 'consulta'
    if (metodo === 'GET' && /^\/people\/[^/]+\/in-catalog$/.test(ruta)) return 'superponer'
    if (/^\/media\/[^/]+\/(progress|user-state)$/.test(ruta)) return 'local'
    if (/^\/(libraries|series)\/[^/]+\/progress-states$/.test(ruta) && metodo === 'GET') return 'local'
    if (/^\/series\/[^/]+\/progress$/.test(ruta) && metodo === 'PUT') return 'local'
    if (ruta === '/watchlist' || ruta.startsWith('/watchlist/')) return 'local'
    if (ruta === '/collections' || ruta.startsWith('/collections/')) return 'local'
    return null
  }

  // ------------------------------------------------------------------ consultas del servidor con el progreso de aquí

  /**
   * El servidor responde `/catalog`, `/home` y las tarjetas viendo el progreso que se le manda (el de este equipo) y no el de su
   * dueño (la API key comparte la fila del admin, specs/04 §2): así el filtro por estado, los episodios vistos de una serie, el
   * orden «reproducido», «continuar viendo» y los destacados salen bien. `null` si el servidor no pudo responder.
   */
  private async conMiProgreso(consulta: 'catalog' | 'home' | 'items', extra: Record<string, unknown> = {}): Promise<unknown | null> {
    return this.remoto('/catalog/con-estado', { consulta, ...extra, estados: this.local.filas() })
  }

  /** Tarjetas del catálogo (película o serie) de `ids`, en ese orden, con el progreso propio de este equipo. */
  private async elementos(ids: string[], progresoDe?: (id: string) => ProgresoLocal | null): Promise<ElementoCatalogo[]> {
    const lista = quitarVacios(ids)
    if (!lista.length) return []
    const r = await this.conMiProgreso('items', { ids: lista })
    if (!esArreglo(r)) return []
    // Dentro de una colección cada película muestra el progreso PROPIO de la colección, no el global.
    return progresoDe ? (r as ElementoCatalogo[]).map((e) => (e.kind === 'pelicula' ? { ...e, progreso: resumen(progresoDe(e.id)) } : e)) : (r as ElementoCatalogo[])
  }

  /** `/catalog` y `/home`. */
  async consultar(ruta: string, query: URLSearchParams): Promise<RespuestaPersonal> {
    if (ruta === '/catalog') {
      const r = await this.conMiProgreso('catalog', { query: Object.fromEntries(query) })
      return r ? ok(r) : error(502, 'No pudimos consultar el catálogo del servidor.')
    }
    const h = (await this.conMiProgreso('home')) as Home | null
    if (!h || typeof h !== 'object') return error(502, 'No pudimos consultar el inicio del servidor.')
    // «Próximas a ver» del servidor es la de su dueño: aquí la lista es la de este equipo.
    return ok({ ...h, proximasAVer: await this.elementos(this.local.proximas(30)) })
  }

  /** `/people/:id/in-catalog`: las tarjetas vienen con el progreso del dueño; se piden de nuevo con el de aquí (y se conserva el rol). */
  async superponer(cuerpo: unknown): Promise<unknown> {
    if (!esArreglo(cuerpo)) return cuerpo
    const originales = cuerpo as (ElementoCatalogo & { rol?: string })[]
    const propias = new Map((await this.elementos(originales.map((e) => e.id))).map((e) => [e.id, e]))
    return originales.flatMap((o) => {
      const e = propias.get(o.id)
      return e ? [{ ...e, ...(o.rol ? { rol: o.rol } : {}) }] : []
    })
  }

  // ------------------------------------------------------------------ rutas locales

  async manejar(p: PeticionPersonal): Promise<RespuestaPersonal> {
    const { metodo, ruta } = p
    const seg = ruta.split('/').filter(Boolean)

    // /media/:id/progress  y  /media/:id/user-state
    if (seg[0] === 'media') {
      const id = seg[1]!
      if (seg[2] === 'user-state') {
        if (metodo !== 'GET') return error(405, 'Método no permitido.')
        return ok({
          progreso: progresoJson(this.local.progreso(id)),
          enProximas: this.local.enProximas(id),
          colecciones: this.local.coleccionesDeItem(id).map((c) => ({ id: c.id, nombre: c.nombre }))
        })
      }
      if (metodo === 'GET') return ok(progresoJson(this.local.progreso(id)))
      if (metodo !== 'PUT') return error(405, 'Método no permitido.')
      const pr = leerProgreso(p.cuerpo)
      if (typeof pr === 'string') return error(400, pr)
      if (pr.estado === 'visto') this.local.marcarVisto(id)
      else if (pr.estado === 'noVisto') this.local.marcarNoVisto(id)
      else this.local.guardarProgreso(id, pr.pos, 'enProgreso', pr.audio, pr.sub)
      return ok(progresoJson(this.local.progreso(id)))
    }

    if (seg[0] === 'libraries') {
      // Las claves son ids de ítem, únicos en el servidor: devolver todo lo guardado sirve igual que filtrar por biblioteca.
      const todos = this.local.estados()
      return ok(Object.fromEntries(Object.entries(todos).map(([id, v]) => [id, v.estado])))
    }

    if (seg[0] === 'series') {
      if (seg[2] === 'progress-states') return ok(this.local.estados())
      const b = (p.cuerpo ?? {}) as { estado?: unknown; temporada?: unknown }
      if (b.estado !== 'visto' && b.estado !== 'noVisto') return error(400, 'estado debe ser "visto" o "noVisto".')
      const temporada = typeof b.temporada === 'number' && Number.isFinite(b.temporada) ? Math.trunc(b.temporada) : null
      const eps = await this.remoto(`/series/${seg[1]}/episodes${temporada == null ? '' : `?temporada=${temporada}`}`)
      if (!esArreglo(eps)) return error(502, 'No pudimos consultar los episodios de la serie.')
      const ids = (eps as { id: string }[]).map((e) => e.id)
      this.local.marcarVarios(ids, b.estado)
      return ok({ ok: true, episodios: ids.length })
    }

    if (seg[0] === 'watchlist') return this.proximas(metodo, seg[1], p.query)
    return this.colecciones(p, seg)
  }

  private async proximas(metodo: string, id: string | undefined, query: URLSearchParams): Promise<RespuestaPersonal> {
    if (metodo === 'GET' && !id) {
      const ids = this.local.proximas()
      if (query.get('formato') === 'catalogo') return ok(await this.elementos(ids))
      const r = await this.remoto(`/catalog/items?ids=${encodeURIComponent(ids.join(','))}`)
      return ok(esArreglo(r) ? r : [])
    }
    if (!id) return error(405, 'Método no permitido.')
    if (metodo === 'PUT') this.local.agregarAProximas(id)
    else if (metodo === 'DELETE') this.local.quitarDeProximas(id)
    else return error(405, 'Método no permitido.')
    return ok({ ok: true })
  }

  /** Colección en el formato de la API: conteo y hasta 4 pósters para el mosaico. */
  private async aJson(cs: ColeccionLocal[]): Promise<Coleccion[]> {
    const ids = cs.map((c) => this.local.idsDeColeccion(c.id))
    const primeros = [...new Set(ids.flatMap((l) => l.slice(0, 4)))]
    const porId = new Map((await this.elementos(primeros)).map((e) => [e.id, e.posterUrl]))
    return cs.map((c, i) => ({
      id: c.id,
      nombre: c.nombre,
      quitarAlVerse: c.quitarAlVerse,
      creadoEn: new Date(c.creadoEn * 1000).toISOString(),
      conteo: ids[i]!.length,
      posters: ids[i]!.slice(0, 4).flatMap((id) => porId.get(id) ?? [])
    }))
  }

  private async colecciones(p: PeticionPersonal, seg: string[]): Promise<RespuestaPersonal> {
    const { metodo } = p
    const body = (p.cuerpo ?? {}) as { nombre?: unknown; quitarAlVerse?: unknown; mediaItemId?: unknown }

    if (!seg[1]) {
      if (metodo === 'GET') return ok(await this.aJson(this.local.listarColecciones()))
      if (metodo !== 'POST') return error(405, 'Método no permitido.')
      if (typeof body.nombre !== 'string' || !body.nombre.trim()) return error(400, 'nombre es requerido.')
      if (body.nombre.trim().length > 200) return error(400, 'El nombre admite hasta 200 caracteres.')
      const id = this.local.crearColeccion(body.nombre, body.quitarAlVerse === true)
      return ok((await this.aJson([this.local.obtenerColeccion(id)!]))[0])
    }

    if (seg[1] === 'item') {
      if (metodo !== 'GET' || !seg[2]) return error(405, 'Método no permitido.')
      return ok(await this.aJson(this.local.coleccionesDeItem(seg[2])))
    }

    const c = this.local.obtenerColeccion(seg[1])
    if (!c) return error(404, 'Colección no encontrada.')
    const sub = seg[2]

    if (!sub) {
      if (metodo === 'GET') return ok((await this.aJson([c]))[0])
      if (metodo === 'DELETE') {
        this.local.eliminarColeccion(c.id)
        return ok({ ok: true })
      }
      if (metodo !== 'PUT') return error(405, 'Método no permitido.')
      if (typeof body.nombre === 'string' && (!body.nombre.trim() || body.nombre.trim().length > 200)) return error(400, 'El nombre debe tener entre 1 y 200 caracteres.')
      if (typeof body.nombre === 'string') this.local.renombrarColeccion(c.id, body.nombre)
      if (typeof body.quitarAlVerse === 'boolean') this.local.setQuitarAlVerse(c.id, body.quitarAlVerse)
      return ok((await this.aJson([this.local.obtenerColeccion(c.id)!]))[0])
    }

    if (sub === 'items') {
      if (metodo === 'GET') {
        const ids = this.local.idsDeColeccion(c.id)
        // Dentro de una colección cada tarjeta muestra el progreso PROPIO de la colección, no el global (RF-62).
        const propios = this.local.progresosDeColeccion(c.id)
        if (p.query.get('formato') === 'catalogo') return ok(await this.elementos(ids, (id) => propios.get(id) ?? null))
        const r = await this.remoto(`/catalog/items?ids=${encodeURIComponent(ids.join(','))}`)
        return ok(esArreglo(r) ? r : [])
      }
      if (metodo === 'POST') {
        if (typeof body.mediaItemId !== 'string') return error(400, 'mediaItemId es requerido.')
        this.local.agregarItemAColeccion(c.id, body.mediaItemId)
        return ok({ ok: true })
      }
      if (metodo === 'DELETE' && seg[3]) {
        this.local.quitarItemDeColeccion(c.id, seg[3])
        return ok({ ok: true })
      }
      return error(405, 'Método no permitido.')
    }

    if (sub === 'continuar-viendo' && metodo === 'GET') {
      const filas = this.local.continuarEnColeccion(c.id)
      const pos = new Map(filas.map((f) => [f.id, f]))
      const els = await this.elementos(filas.map((f) => f.id), (id) => ({ posicionSegundos: pos.get(id)?.posicionSegundos ?? 0, estado: 'enProgreso', pistaAudioId: null, pistaSubtituloId: null }))
      return ok(els)
    }

    if (sub === 'progress' && seg[3]) {
      const id = seg[3]
      if (metodo === 'GET') return ok(progresoJson(this.local.progresoEnColeccion(c.id, id)))
      if (metodo !== 'PUT') return error(405, 'Método no permitido.')
      const pr = leerProgreso(p.cuerpo)
      if (typeof pr === 'string') return error(400, pr)
      if (pr.estado === 'visto') this.local.marcarVistoEnColeccion(c.id, id)
      else if (pr.estado === 'noVisto') this.local.marcarNoVistoEnColeccion(c.id, id)
      else this.local.guardarProgresoEnColeccion(c.id, id, pr.pos, 'enProgreso', pr.audio, pr.sub)
      return ok(progresoJson(this.local.progresoEnColeccion(c.id, id)))
    }

    return error(404, 'Ruta no encontrada.')
  }
}
