import type { FastifyInstance, FastifyReply, FastifyRequest } from 'fastify'
import type { Coleccion, ElementoCatalogo, Progreso } from '../../../shared/api-types'
import { ESTADOS_VISTO, type EstadoVisto } from '../../../shared/enums'
import type { ColeccionDb, ProgresoDb } from '../../repositories/escritura'
import { error } from '../auth'
import type { Contexto } from '../app'
import { mediaItemToJson } from '../mappers'

const SOLO_LECTURA = 'La base de datos está en solo lectura (es de una versión más nueva de CinemaLove).'

const progresoToJson = (p: ProgresoDb | null): Progreso =>
  p
    ? {
        posicionSegundos: p.posicionSegundos,
        estado: p.estado,
        ...(p.pistaAudioId ? { pistaAudioId: p.pistaAudioId } : {}),
        ...(p.pistaSubtituloId ? { pistaSubtituloId: p.pistaSubtituloId } : {})
      }
    : { posicionSegundos: 0, estado: 'noVisto' }

const coleccionToJson = (c: ColeccionDb): Coleccion => ({
  id: c.id,
  nombre: c.nombre,
  quitarAlVerse: c.quitarAlVerse,
  creadoEn: new Date(c.creadoEn * 1000).toISOString(),
  conteo: c.conteo,
  posters: c.posters
})

interface BodyProgreso {
  estado?: unknown
  posicionSegundos?: unknown
  pistaAudioId?: unknown
  pistaSubtituloId?: unknown
}

/** Valida el body de un PUT de progreso (compartido por el global y el de colección). */
function leerProgreso(body: unknown): { estado: EstadoVisto | null; pos: number; audio: string | null; sub: string | null } | string {
  const b = (body ?? {}) as BodyProgreso
  if (b.estado !== undefined && b.estado !== null && !(ESTADOS_VISTO as readonly unknown[]).includes(b.estado)) {
    return `estado inválido: ${String(b.estado)}`
  }
  const pos = typeof b.posicionSegundos === 'number' && Number.isFinite(b.posicionSegundos) ? Math.max(0, Math.trunc(b.posicionSegundos)) : 0
  return {
    estado: (b.estado as EstadoVisto | undefined) ?? null,
    pos,
    audio: typeof b.pistaAudioId === 'string' ? b.pistaAudioId : null,
    sub: typeof b.pistaSubtituloId === 'string' ? b.pistaSubtituloId : null
  }
}

export function rutasUsuario(app: FastifyInstance, ctx: Contexto): void {
  const { repos, escritura, catalogo } = ctx
  const uid = (req: FastifyRequest): string | null => req.principal!.usuarioId

  /** Listas y colecciones: no disponibles con API key (el cliente usa su base local), como en Flutter. */
  const sinListas = (req: FastifyRequest, reply: FastifyReply): FastifyReply | null =>
    req.principal!.tipo === 'token'
      ? error(reply, 403, 'Las colecciones y "Próximas a ver" requieren conexión por usuario/contraseña, no por API key.')
      : null
  const protegido = (reply: FastifyReply): FastifyReply | null => (ctx.soloLectura ? error(reply, 409, SOLO_LECTURA) : null)

  // --- progreso [v1-legado]
  app.get<{ Params: { id: string } }>('/api/v1/media/:id/progress', async (req, reply) => {
    if (!repos.obtenerItem(req.params.id)) return error(reply, 404, 'Ítem no encontrado.')
    return progresoToJson(escritura.obtenerProgreso(req.params.id, uid(req)))
  })

  app.put<{ Params: { id: string } }>('/api/v1/media/:id/progress', async (req, reply) => {
    if (!repos.obtenerItem(req.params.id)) return error(reply, 404, 'Ítem no encontrado.')
    const p = leerProgreso(req.body)
    if (typeof p === 'string') return error(reply, 400, p)
    const bloqueo = protegido(reply)
    if (bloqueo) return bloqueo
    const id = req.params.id
    if (p.estado === 'visto') escritura.marcarVisto(id, uid(req))
    else if (p.estado === 'noVisto') escritura.marcarNoVisto(id, uid(req))
    else escritura.guardarProgreso(id, uid(req), p.pos, 'enProgreso', p.audio, p.sub)
    return progresoToJson(escritura.obtenerProgreso(id, uid(req)))
  })

  // --- nuevos: estados agregados (evitan N peticiones desde la UI)
  app.get<{ Params: { id: string } }>('/api/v1/libraries/:id/progress-states', async (req, reply) => {
    if (!repos.obtenerBiblioteca(req.params.id)) return error(reply, 404, 'Biblioteca no encontrada.')
    return escritura.estadosDeBiblioteca(req.params.id, uid(req))
  })

  app.get<{ Params: { id: string } }>('/api/v1/series/:id/progress-states', async (req, reply) => {
    if (!repos.obtenerSerie(req.params.id)) return error(reply, 404, 'Serie no encontrada.')
    return escritura.estadosDeSerie(req.params.id, uid(req))
  })

  /** Marca como vista/no vista una temporada (`temporada`) o la serie entera. */
  app.put<{ Params: { id: string } }>('/api/v1/series/:id/progress', async (req, reply) => {
    if (!repos.obtenerSerie(req.params.id)) return error(reply, 404, 'Serie no encontrada.')
    const b = (req.body ?? {}) as { estado?: unknown; temporada?: unknown }
    if (b.estado !== 'visto' && b.estado !== 'noVisto') return error(reply, 400, 'estado debe ser "visto" o "noVisto".')
    const temporada = typeof b.temporada === 'number' && Number.isFinite(b.temporada) ? Math.trunc(b.temporada) : null
    const bloqueo = protegido(reply)
    if (bloqueo) return bloqueo
    return { ok: true, episodios: escritura.marcarSerie(req.params.id, temporada, b.estado, uid(req)) }
  })

  /** Todo lo personal de un ítem en una sola petición: progreso, si está en Próximas a ver y sus colecciones. */
  app.get<{ Params: { id: string } }>('/api/v1/media/:id/user-state', async (req, reply) => {
    if (!repos.obtenerItem(req.params.id)) return error(reply, 404, 'Ítem no encontrado.')
    const conListas = req.principal!.tipo !== 'token'
    return {
      progreso: progresoToJson(escritura.obtenerProgreso(req.params.id, uid(req))),
      enProximas: conListas && escritura.estaEnProximas(req.params.id, uid(req)),
      colecciones: conListas ? escritura.coleccionesDeItem(req.params.id, uid(req)).map((c) => ({ id: c.id, nombre: c.nombre })) : []
    }
  })

  // --- Próximas a ver [v1-legado]
  app.get<{ Querystring: { formato?: string } }>('/api/v1/watchlist', async (req, reply) => {
    const denegado = sinListas(req, reply)
    if (denegado) return denegado
    const ids = repos.listarProximasAVer(uid(req)).map((p) => p.mediaItemId)
    if (req.query.formato === 'catalogo') return catalogo.elementosPorIds(ids, uid(req))
    return ids.flatMap((id) => {
      const item = repos.obtenerItem(id)
      return item && item.estadoArchivo === 'ok' ? [mediaItemToJson(item)] : []
    })
  })

  app.put<{ Params: { id: string } }>('/api/v1/watchlist/:id', async (req, reply) => {
    const denegado = sinListas(req, reply)
    if (denegado) return denegado
    if (!repos.obtenerItem(req.params.id)) return error(reply, 404, 'Ítem no encontrado.')
    const bloqueo = protegido(reply)
    if (bloqueo) return bloqueo
    escritura.agregarAProximas(req.params.id, uid(req))
    return { ok: true }
  })

  app.delete<{ Params: { id: string } }>('/api/v1/watchlist/:id', async (req, reply) => {
    const denegado = sinListas(req, reply)
    if (denegado) return denegado
    const bloqueo = protegido(reply)
    if (bloqueo) return bloqueo
    escritura.quitarDeProximas(req.params.id, uid(req))
    return { ok: true }
  })

  // --- colecciones [v1-legado]
  app.get('/api/v1/collections', async (req, reply) => {
    const denegado = sinListas(req, reply)
    return denegado ?? escritura.listarColecciones(uid(req)).map(coleccionToJson)
  })

  app.post('/api/v1/collections', async (req, reply) => {
    const denegado = sinListas(req, reply)
    if (denegado) return denegado
    const b = (req.body ?? {}) as { nombre?: unknown; quitarAlVerse?: unknown }
    if (typeof b.nombre !== 'string' || !b.nombre.trim()) return error(reply, 400, 'nombre es requerido.')
    if (b.nombre.trim().length > 200) return error(reply, 400, 'El nombre admite hasta 200 caracteres.')
    const bloqueo = protegido(reply)
    if (bloqueo) return bloqueo
    const id = escritura.crearColeccion(uid(req), b.nombre, b.quitarAlVerse === true)
    return coleccionToJson(escritura.obtenerColeccion(id, uid(req))!)
  })

  /** Resuelve la colección del principal o responde 403/404. */
  const coleccionDe = (req: FastifyRequest<{ Params: { id: string } }>, reply: FastifyReply): ColeccionDb | FastifyReply => {
    const denegado = sinListas(req, reply)
    if (denegado) return denegado
    return escritura.obtenerColeccion(req.params.id, uid(req)) ?? error(reply, 404, 'Colección no encontrada.')
  }
  const esRespuesta = (x: ColeccionDb | FastifyReply): x is FastifyReply => 'send' in x

  app.get<{ Params: { id: string } }>('/api/v1/collections/:id', async (req, reply) => {
    const c = coleccionDe(req, reply)
    return esRespuesta(c) ? c : coleccionToJson(c)
  })

  app.put<{ Params: { id: string } }>('/api/v1/collections/:id', async (req, reply) => {
    const c = coleccionDe(req, reply)
    if (esRespuesta(c)) return c
    const b = (req.body ?? {}) as { nombre?: unknown; quitarAlVerse?: unknown }
    if (typeof b.nombre === 'string' && (!b.nombre.trim() || b.nombre.trim().length > 200)) {
      return error(reply, 400, 'El nombre debe tener entre 1 y 200 caracteres.')
    }
    const bloqueo = protegido(reply)
    if (bloqueo) return bloqueo
    if (typeof b.nombre === 'string') escritura.renombrarColeccion(c.id, b.nombre)
    if (typeof b.quitarAlVerse === 'boolean') escritura.setQuitarAlVerse(c.id, b.quitarAlVerse)
    return coleccionToJson(escritura.obtenerColeccion(c.id, uid(req))!)
  })

  app.delete<{ Params: { id: string } }>('/api/v1/collections/:id', async (req, reply) => {
    const c = coleccionDe(req, reply)
    if (esRespuesta(c)) return c
    const bloqueo = protegido(reply)
    if (bloqueo) return bloqueo
    escritura.eliminarColeccion(c.id)
    return { ok: true }
  })

  app.get<{ Params: { id: string } }>('/api/v1/collections/item/:id', async (req, reply) => {
    const denegado = sinListas(req, reply)
    return denegado ?? escritura.coleccionesDeItem(req.params.id, uid(req)).map(coleccionToJson)
  })

  app.get<{ Params: { id: string }; Querystring: { formato?: string } }>('/api/v1/collections/:id/items', async (req, reply) => {
    const c = coleccionDe(req, reply)
    if (esRespuesta(c)) return c
    const ids = escritura.idsDeColeccion(c.id)
    if (req.query.formato === 'catalogo') {
      // Dentro de una colección cada tarjeta muestra el progreso PROPIO de la colección (RF-62), no el global.
      const propios = escritura.progresosDeColeccion(c.id, uid(req))
      return catalogo.elementosPorIds(ids, uid(req)).map((el) =>
        el.kind === 'pelicula'
          ? { ...el, progreso: propios.has(el.id) ? { estado: propios.get(el.id)!.estado, posicionSegundos: propios.get(el.id)!.posicionSegundos } : null }
          : el
      )
    }
    return ids.flatMap((id) => {
      const item = repos.obtenerItem(id)
      return item ? [mediaItemToJson(item)] : []
    })
  })

  app.post<{ Params: { id: string } }>('/api/v1/collections/:id/items', async (req, reply) => {
    const c = coleccionDe(req, reply)
    if (esRespuesta(c)) return c
    const mediaItemId = (req.body as { mediaItemId?: unknown } | null)?.mediaItemId
    if (typeof mediaItemId !== 'string') return error(reply, 400, 'mediaItemId es requerido.')
    if (!repos.obtenerItem(mediaItemId)) return error(reply, 404, 'Ítem no encontrado.')
    const bloqueo = protegido(reply)
    if (bloqueo) return bloqueo
    escritura.agregarItemAColeccion(c.id, mediaItemId)
    return { ok: true }
  })

  app.delete<{ Params: { id: string; mediaItemId: string } }>('/api/v1/collections/:id/items/:mediaItemId', async (req, reply) => {
    const c = coleccionDe(req, reply)
    if (esRespuesta(c)) return c
    const bloqueo = protegido(reply)
    if (bloqueo) return bloqueo
    escritura.quitarItemDeColeccion(c.id, req.params.mediaItemId)
    return { ok: true }
  })

  app.get<{ Params: { id: string }; Querystring: { formato?: string } }>('/api/v1/collections/:id/continuar-viendo', async (req, reply) => {
    const c = coleccionDe(req, reply)
    if (esRespuesta(c)) return c
    const filas = escritura.continuarEnColeccion(c.id, uid(req))
    if (req.query.formato === 'catalogo') {
      const pos = new Map(filas.map((f) => [f.id, f.posicionSegundos]))
      return catalogo.elementosPorIds(filas.map((f) => f.id), uid(req)).map((el) =>
        el.kind === 'pelicula' ? { ...el, progreso: { estado: 'enProgreso' as const, posicionSegundos: pos.get(el.id) ?? 0 } } : el
      ) satisfies ElementoCatalogo[]
    }
    return filas.flatMap((f) => {
      const item = repos.obtenerItem(f.id)
      return item ? [{ ...mediaItemToJson(item), posicionSegundos: f.posicionSegundos }] : []
    })
  })

  app.get<{ Params: { id: string; mediaItemId: string } }>('/api/v1/collections/:id/progress/:mediaItemId', async (req, reply) => {
    const c = coleccionDe(req, reply)
    if (esRespuesta(c)) return c
    return progresoToJson(escritura.obtenerProgresoEnColeccion(c.id, req.params.mediaItemId, uid(req)))
  })

  app.put<{ Params: { id: string; mediaItemId: string } }>('/api/v1/collections/:id/progress/:mediaItemId', async (req, reply) => {
    const c = coleccionDe(req, reply)
    if (esRespuesta(c)) return c
    const p = leerProgreso(req.body)
    if (typeof p === 'string') return error(reply, 400, p)
    const bloqueo = protegido(reply)
    if (bloqueo) return bloqueo
    const { mediaItemId } = req.params
    if (p.estado === 'visto') escritura.marcarVistoEnColeccion(c.id, mediaItemId, uid(req))
    else if (p.estado === 'noVisto') escritura.marcarNoVistoEnColeccion(c.id, mediaItemId, uid(req))
    else escritura.guardarProgresoEnColeccion(c.id, mediaItemId, uid(req), p.pos, 'enProgreso', p.audio, p.sub)
    return progresoToJson(escritura.obtenerProgresoEnColeccion(c.id, mediaItemId, uid(req)))
  })

  // --- personas (nuevo)
  /** Ficha de persona: datos del catálogo + biografía de TMDb si hay clave y red. */
  app.get<{ Params: { id: string } }>('/api/v1/people/:id', async (req, reply) => {
    const id = Number(req.params.id)
    if (!Number.isInteger(id)) return error(reply, 400, 'El id de persona debe ser numérico.')
    const local = escritura.datosLocalesPersona(id)
    const tmdb = await ctx.personas.obtener(id)
    if (!local && !tmdb) return error(reply, 404, 'Persona no encontrada.')
    return {
      id,
      nombre: tmdb?.nombre ?? local?.nombre ?? '',
      fotoUrl: tmdb?.fotoUrl ?? local?.fotoUrl ?? null,
      departamento: tmdb?.departamento ?? (local ? (local.enEquipo ? 'Equipo' : 'Actuación') : null),
      biografia: tmdb?.biografia ?? null,
      nacimiento: tmdb?.nacimiento ?? null,
      fallecimiento: tmdb?.fallecimiento ?? null,
      lugarNacimiento: tmdb?.lugarNacimiento ?? null
    }
  })
  app.get<{ Params: { id: string } }>('/api/v1/people/:id/in-catalog', async (req, reply) => {
    const id = Number(req.params.id)
    if (!Number.isInteger(id)) return error(reply, 400, 'El id de persona debe ser numérico.')
    const hallados = escritura.enCatalogoPersona(id)
    const porId = new Map(catalogo.elementosPorIdsCualquiera(hallados.map((h) => h.id), uid(req)).map((e) => [e.id, e]))
    return hallados.flatMap((h) => {
      const el = porId.get(h.id)
      return el ? [{ ...el, ...(h.rol ? { rol: h.rol } : {}) }] : []
    })
  })
}
