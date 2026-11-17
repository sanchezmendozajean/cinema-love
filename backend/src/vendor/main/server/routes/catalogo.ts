import { existsSync } from 'node:fs'
import { extname, isAbsolute, join, normalize, relative, resolve } from 'node:path'
import type { FastifyInstance, FastifyReply } from 'fastify'
import type { FiltrosCatalogo, OrdenCatalogo } from '../../../shared/api-types'
import { ESTADOS_VISTO, TIPOS_BIBLIOTECA } from '../../../shared/enums'
import type { EstadoExterno } from '../../repositories/catalogo'
import type { Biblioteca } from '../../repositories/tipos'
import { error } from '../auth'
import type { Contexto } from '../app'
import { extraToJson, libraryToJson, mediaItemToJson, seasonToJson, serieToJson } from '../mappers'
import { MIME_VIDEO, servirArchivo } from '../range'

const ORDENES = new Set(['titulo', 'anio', 'agregado', 'reproducido'])

const lista = (v: unknown): string[] | undefined => {
  const s = Array.isArray(v) ? v.join(',') : typeof v === 'string' ? v : ''
  const partes = s.split(',').map((x) => x.trim()).filter(Boolean)
  return partes.length ? partes : undefined
}
const entero = (v: unknown): number | undefined => {
  const n = typeof v === 'string' && v.trim() !== '' ? Number(v) : NaN
  return Number.isFinite(n) ? Math.trunc(n) : undefined
}

/** Valida los parámetros de `/catalog`; devuelve el mensaje de error si hay alguno inválido. */
export function parsearFiltros(query: Record<string, unknown>): FiltrosCatalogo | string {
  const f: FiltrosCatalogo = {}
  if (typeof query.q === 'string' && query.q.trim()) f.q = query.q
  f.generos = lista(query.generos)
  f.bibliotecas = lista(query.bibliotecas)
  if (query.tipo !== undefined) {
    if (!(TIPOS_BIBLIOTECA as readonly unknown[]).includes(query.tipo)) return 'tipo inválido (pelicula | serie).'
    f.tipo = query.tipo as 'pelicula' | 'serie'
  }
  f.anioDesde = entero(query.anioDesde)
  f.anioHasta = entero(query.anioHasta)
  if (query.estado !== undefined) {
    if (!(ESTADOS_VISTO as readonly unknown[]).includes(query.estado)) return 'estado inválido.'
    f.estado = query.estado as FiltrosCatalogo['estado']
  }
  if (query.orden !== undefined) {
    const o = String(query.orden)
    if (!ORDENES.has(o.replace(/^-/, ''))) return 'orden inválido.'
    f.orden = o as OrdenCatalogo
  }
  f.pagina = entero(query.pagina)
  f.tamano = entero(query.tamano)
  return f
}

/** Ruta física de un archivo de la biblioteca; null si se sale de su carpeta (path traversal). */
export function rutaSegura(lib: Biblioteca, rutaRelativa: string): string | null {
  const raiz = resolve(lib.rutaActual)
  const ruta = resolve(normalize(join(raiz, rutaRelativa)))
  const rel = relative(raiz, ruta)
  return rel && !rel.startsWith('..') && !isAbsolute(rel) ? ruta : null
}

export function rutasCatalogo(app: FastifyInstance, ctx: Contexto): void {
  const { repos, catalogo } = ctx
  const noEncontrado = (r: FastifyReply, que: string): FastifyReply => error(r, 404, `${que} no encontrado.`)

  // --- [v1-legado]
  app.get('/api/v1/libraries', async () => {
    const resumen = catalogo.resumenBibliotecas()
    return repos.listarBibliotecas().map((l) => ({ ...libraryToJson(l), ...(resumen.get(l.id) ?? { conteo: 0, sinMetadatos: 0, backdrops: [] }) }))
  })

  app.get<{ Params: { id: string } }>('/api/v1/libraries/:id/items', async (req, reply) => {
    const lib = repos.obtenerBiblioteca(req.params.id)
    if (!lib) return error(reply, 404, 'Biblioteca no encontrada.')
    if (lib.tipo !== 'pelicula') return error(reply, 400, 'Esta biblioteca no es de tipo película.')
    return repos.peliculasVisibles(lib.id).map((i) => mediaItemToJson(i))
  })

  app.get<{ Params: { id: string } }>('/api/v1/libraries/:id/series', async (req, reply) => {
    const lib = repos.obtenerBiblioteca(req.params.id)
    if (!lib) return error(reply, 404, 'Biblioteca no encontrada.')
    if (lib.tipo !== 'serie') return error(reply, 400, 'Esta biblioteca no es de tipo serie.')
    return repos.seriesVisibles(lib.id).map((s) => serieToJson(s))
  })

  app.get<{ Params: { id: string } }>('/api/v1/items/:id', async (req, reply) => {
    const item = repos.obtenerItem(req.params.id)
    return item ? mediaItemToJson(item, true) : error(reply, 404, 'Ítem no encontrado.')
  })

  app.get<{ Params: { id: string } }>('/api/v1/items/:id/extras', async (req) =>
    repos.listarExtras(req.params.id).map(extraToJson)
  )

  app.get<{ Params: { id: string } }>('/api/v1/series/:id', async (req, reply) => {
    const serie = repos.obtenerSerie(req.params.id)
    return serie ? serieToJson(serie, true) : error(reply, 404, 'Serie no encontrada.')
  })

  app.get<{ Params: { id: string } }>('/api/v1/series/:id/seasons', async (req) =>
    repos.listarTemporadas(req.params.id).map(seasonToJson)
  )

  app.get<{ Params: { id: string }; Querystring: { temporada?: string } }>(
    '/api/v1/series/:id/episodes',
    async (req) => {
      const t = entero(req.query.temporada)
      // Con `?temporada=` no numérico Flutter devuelve lista vacía (compara con null).
      if (req.query.temporada !== undefined && t === undefined) return []
      return repos.episodiosVisibles(req.params.id, t).map((e) => ({ ...mediaItemToJson(e), sinopsis: e.sinopsis }))
    }
  )

  const servirVideo = (req: Parameters<typeof servirArchivo>[0], reply: FastifyReply, lib: Biblioteca | null, rel: string): FastifyReply => {
    if (!lib) return error(reply, 404, 'Biblioteca no encontrada.')
    const ruta = rutaSegura(lib, rel)
    if (!ruta) return error(reply, 400, 'Ruta de archivo inválida.')
    if (!existsSync(ruta)) return error(reply, 404, 'El archivo no se encuentra en la ruta actual.')
    return servirArchivo(req, reply, ruta, MIME_VIDEO[extname(ruta).toLowerCase()] ?? 'application/octet-stream')
  }

  app.get<{ Params: { id: string } }>('/api/v1/media/:id/stream', async (req, reply) => {
    const item = repos.obtenerItem(req.params.id)
    if (!item) return error(reply, 404, 'Ítem no encontrado.')
    return servirVideo(req, reply, repos.obtenerBiblioteca(item.libraryId), item.rutaRelativa)
  })

  app.get<{ Params: { id: string } }>('/api/v1/extras/:id/stream', async (req, reply) => {
    const extra = repos.obtenerExtra(req.params.id)
    if (!extra) return noEncontrado(reply, 'Extra')
    const item = repos.obtenerItem(extra.mediaItemId)
    if (!item) return error(reply, 404, 'Película asociada no encontrada.')
    return servirVideo(req, reply, repos.obtenerBiblioteca(item.libraryId), extra.rutaRelativa)
  })

  // --- nuevos
  app.get('/api/v1/catalog', async (req, reply) => {
    const f = parsearFiltros(req.query as Record<string, unknown>)
    if (typeof f === 'string') return error(reply, 400, f)
    return catalogo.buscar(f, req.principal!.usuarioId)
  })

  app.get('/api/v1/home', async (req) => catalogo.home(req.principal!.usuarioId))

  /**
   * Tarjetas de ítems concretos (`?ids=a,b,c`, hasta 200, en ese orden). Las usa el cliente con token para mostrar sus
   * listas locales («Próximas a ver», colecciones, «continuar viendo»): el servidor pone los datos del catálogo y el
   * cliente su propio progreso.
   */
  const idsDe = (q: unknown): string[] => {
    const crudo = (q as { ids?: unknown } | undefined)?.ids
    return typeof crudo === 'string' ? [...new Set(crudo.split(',').filter(Boolean))].slice(0, 200) : []
  }
  /**
   * Las mismas consultas de `/catalog`, `/home` y `/catalog/items`, pero viendo el progreso que manda el cliente (el de un
   * cliente con token, que lo guarda en su equipo) y no el del servidor. Aditiva: los clientes Flutter no la usan.
   * Body: `{ consulta: 'catalog'|'home'|'items', query?, ids?, estados: [{id, estado, posicionSegundos, ultima}] }`.
   */
  app.post('/api/v1/catalog/con-estado', async (req, reply) => {
    const b = (req.body ?? {}) as { consulta?: unknown; query?: unknown; ids?: unknown; estados?: unknown }
    if (!Array.isArray(b.estados) || b.estados.length > 100_000) return error(reply, 400, 'estados debe ser una lista (hasta 100 000).')
    const estados: EstadoExterno[] = []
    for (const e of b.estados as Record<string, unknown>[]) {
      if (typeof e?.id !== 'string' || !(ESTADOS_VISTO as readonly unknown[]).includes(e.estado)) return error(reply, 400, 'Un estado no es válido.')
      estados.push({
        id: e.id,
        estado: e.estado as EstadoExterno['estado'],
        posicionSegundos: typeof e.posicionSegundos === 'number' ? Math.max(0, Math.trunc(e.posicionSegundos)) : 0,
        ultima: typeof e.ultima === 'number' ? Math.trunc(e.ultima) : null
      })
    }
    switch (b.consulta) {
      case 'catalog': {
        const f = parsearFiltros((b.query ?? {}) as Record<string, unknown>)
        if (typeof f === 'string') return error(reply, 400, f)
        return catalogo.conEstadosExternos(estados, () => catalogo.buscar(f, null))
      }
      case 'home':
        return catalogo.conEstadosExternos(estados, () => catalogo.home(null))
      case 'items': {
        const ids = Array.isArray(b.ids) ? b.ids.filter((x): x is string => typeof x === 'string').slice(0, 200) : []
        return catalogo.conEstadosExternos(estados, () => catalogo.elementosPorIdsCualquiera(ids, null))
      }
      default:
        return error(reply, 400, 'consulta debe ser catalog, home o items.')
    }
  })

  app.get('/api/v1/catalog/items', async (req) => catalogo.elementosPorIdsCualquiera(idsDe(req.query), req.principal!.usuarioId))
  app.get('/api/v1/catalog/continuar', async (req) => catalogo.resumenesContinuar(idsDe(req.query)))
}
