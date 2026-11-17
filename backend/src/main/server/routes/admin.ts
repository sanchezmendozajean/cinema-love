import type { FastifyInstance, FastifyReply, FastifyRequest } from 'fastify'
import { TIPOS_BIBLIOTECA, type TipoBiblioteca } from '../../../shared/enums'
import { JobEnCurso, type ContextoJob, type Job } from '../../jobs/job-manager'
import { CarpetaNoDisponible, escanearBiblioteca } from '../../scanner/escaner'
import { asignarManual, editarMetadata, importarMetadata, type EdicionManual } from '../../scanner/metadata'
import { TmdbError } from '../../scanner/tmdb-client'
import { ErrorAdmin, actualizarBiblioteca, crearBiblioteca, eliminarBiblioteca, eliminarHuerfanos, enmascarar, homologarNombre, infoArchivo, listarBibliotecas, listarHuerfanos, revincular } from '../../services/admin-service'
import type { AjustesTranscodificacion } from '../../../shared/playback-types'
import { infoDatos, limpiarImagenes, limpiarTemporales } from '../../services/datos-service'
import { error } from '../auth'
import { ErrorServidor } from '../servidor-publico'
import { rutasAdminServidor } from './admin-servidor'
import type { Contexto } from '../app'

const CLAVE_TMDB = 'tmdb_api_key'
const SOLO_LECTURA = 'La base de datos está en solo lectura (es de una versión más nueva de CinemaLove).'

interface BibliotecaFila {
  id: string
  nombre: string
  tipo: TipoBiblioteca
  ruta_actual: string
}

/**
 * Administración (specs/04 §7). SOLO en el listener interno y con el principal admin: ni la API key ni un usuario
 * remoto llegan aquí aunque conozcan la ruta.
 */
export function rutasAdmin(app: FastifyInstance, ctx: Contexto): void {
  if (!ctx.esInterno) return
  const { jobs, tmdb } = ctx

  // Todo lo que cuelga de /admin exige admin; las escrituras además exigen una base que se pueda escribir.
  app.addHook('preHandler', async (req, reply) => {
    if (!req.url.startsWith('/api/v1/admin/')) return
    if (req.principal?.tipo !== 'admin') return error(reply, 403, 'Requiere permisos de administrador.')
    if (req.method !== 'GET' && ctx.soloLectura) return error(reply, 409, SOLO_LECTURA)
  })

  const manejar = (reply: FastifyReply, e: unknown): FastifyReply => {
    if (e instanceof ErrorAdmin || e instanceof ErrorServidor) return error(reply, e.status, e.message)
    if (e instanceof JobEnCurso) return reply.code(409).send({ error: e.message, jobId: e.job.id })
    if (e instanceof TmdbError) return error(reply, e.status === 401 ? 400 : 502, e.message)
    throw e
  }
  const envolver =
    <T extends FastifyRequest>(fn: (req: T, reply: FastifyReply) => Promise<unknown> | unknown) =>
    async (req: T, reply: FastifyReply): Promise<unknown> => {
      try {
        return await fn(req, reply)
      } catch (e) {
        return manejar(reply, e)
      }
    }
  const claveTmdb = (): string => {
    const k = ctx.auth.ajuste(CLAVE_TMDB)
    if (!k?.trim()) throw new ErrorAdmin('Configura tu API key de TMDb en Ajustes antes de buscar metadata.', 412)
    return k.trim()
  }
  const biblioteca = (id: string): BibliotecaFila => {
    const b = ctx.db.prepare('SELECT id, nombre, tipo, ruta_actual FROM libraries WHERE id = ?').get(id) as BibliotecaFila | undefined
    if (!b) throw new ErrorAdmin('La biblioteca no existe.', 404)
    return b
  }
  const vista = (j: Job, conLineas = false): unknown => (conLineas ? j : jobs.resumen(j))

  // ------------------------------------------------------------- trabajos

  /** Escaneo de una biblioteca como trabajo; el resultado incluye qué se reconcilió por hash para la pantalla de revisión. */
  const escanear = (b: BibliotecaFila): Job =>
    jobs.iniciar({ tipo: 'escaneo', bibliotecaId: b.id, titulo: `Escaneando «${b.nombre}»` }, (c) => correrEscaneo(b, c))

  async function correrEscaneo(b: BibliotecaFila, c: ContextoJob): Promise<unknown> {
    c.linea(`Biblioteca «${b.nombre}» — ${b.ruta_actual}`)
    try {
      const r = await escanearBiblioteca(ctx.db, { id: b.id, tipo: b.tipo, rutaActual: b.ruta_actual }, {
        senal: c.senal,
        onProgreso: (p) => {
          if (p.fase === 'listando') c.linea(`Listando archivos… ${p.hechos}/${p.total}`)
          else c.progreso(p.hechos, p.total)
        }
      })
      c.linea(
        `Listo: ${r.nuevos} nuevo(s), ${r.reconciliados} reconciliado(s), ${r.noEncontrados} sin archivo` +
          (b.tipo === 'serie' ? `, ${r.seriesNuevas} serie(s) nueva(s)` : `, ${r.extrasNuevos} extra(s) nuevo(s)`)
      )
      const marcas = r.reconciliadosPorHashIds.map(() => '?').join(',')
      const reconciliados = r.reconciliadosPorHashIds.length
        ? (ctx.db.prepare(`SELECT id, titulo, ruta_relativa AS rutaRelativa FROM media_items WHERE id IN (${marcas})`).all(...r.reconciliadosPorHashIds) as { id: string; titulo: string | null; rutaRelativa: string }[])
        : []
      return { ...r, reconciliadosPorHashIds: undefined, reconciliadosPorHash: reconciliados }
    } catch (e) {
      if (e instanceof CarpetaNoDisponible) {
        c.linea(`No se pudo escanear: ${e.message}`)
        throw new Error('La carpeta de la biblioteca no está disponible (¿disco desconectado?). No se modificó nada.', { cause: e })
      }
      throw e
    }
  }

  app.post('/api/v1/admin/bibliotecas/:id/escanear', envolver(async (req) => vista(escanear(biblioteca((req.params as { id: string }).id)))))

  app.post('/api/v1/admin/escanear-todo', envolver(async () => {
    const todas = ctx.db.prepare('SELECT id, nombre, tipo, ruta_actual FROM libraries ORDER BY creado_en').all() as BibliotecaFila[]
    if (!todas.length) throw new ErrorAdmin('Aún no hay bibliotecas.')
    return vista(
      jobs.iniciar({ tipo: 'escaneo', bibliotecaId: null, titulo: 'Escaneando todas las bibliotecas' }, async (c) => {
        const resultados: unknown[] = []
        for (const b of todas) {
          if (c.senal.aborted) break
          try {
            resultados.push({ biblioteca: b.nombre, ...((await correrEscaneo(b, c)) as object) })
          } catch (e) {
            c.linea(`«${b.nombre}»: ${e instanceof Error ? e.message : 'error inesperado'}`)
          }
        }
        return { bibliotecas: resultados }
      })
    )
  }))

  app.post('/api/v1/admin/bibliotecas/:id/metadata', envolver(async (req) => {
    const b = biblioteca((req.params as { id: string }).id)
    const modo = (req.body as { modo?: unknown } | null)?.modo
    if (modo !== 'faltantes' && modo !== 'todo') throw new ErrorAdmin("modo debe ser 'faltantes' o 'todo'.")
    const apiKey = claveTmdb()
    return vista(
      jobs.iniciar({ tipo: 'metadata', bibliotecaId: b.id, titulo: `Metadata de «${b.nombre}»` }, (c) =>
        importarMetadata(ctx.db, tmdb, { id: b.id, tipo: b.tipo, rutaActual: b.ruta_actual }, apiKey, {
          soloFaltantes: modo === 'faltantes',
          senal: c.senal,
          onEvento: (e) => (e.tipo === 'linea' ? c.linea(e.texto) : c.progreso(e.procesados, e.total))
        })
      )
    )
  }))

  app.get('/api/v1/admin/jobs', async () => jobs.listar().map((j) => jobs.resumen(j)))

  app.get('/api/v1/admin/jobs/:id', envolver(async (req) => {
    const j = jobs.obtener((req.params as { id: string }).id)
    if (!j) throw new ErrorAdmin('El trabajo no existe.', 404)
    return vista(j, true)
  }))

  app.post('/api/v1/admin/jobs/:id/cancelar', envolver(async (req) => {
    const id = (req.params as { id: string }).id
    if (!jobs.obtener(id)) throw new ErrorAdmin('El trabajo no existe.', 404)
    return { cancelado: jobs.cancelar(id) }
  }))

  /** SSE: `job` (estado/progreso) y `linea` (registro). Al conectar manda el estado actual de los trabajos en curso. */
  app.get('/api/v1/admin/jobs-eventos', (req, reply) => {
    reply.hijack()
    const res = reply.raw
    res.writeHead(200, { 'Content-Type': 'text/event-stream', 'Cache-Control': 'no-cache', Connection: 'keep-alive', 'Access-Control-Allow-Origin': (req.headers.origin as string | undefined) ?? '*' })
    const enviar = (evento: string, datos: unknown): void => void res.write(`event: ${evento}\ndata: ${JSON.stringify(datos)}\n\n`)
    const onCambio = (j: unknown): void => enviar('job', j)
    const onLinea = (l: unknown): void => enviar('linea', l)
    jobs.on('cambio', onCambio)
    jobs.on('linea', onLinea)
    for (const j of jobs.listar()) enviar('job', jobs.resumen(j))
    const latido = setInterval(() => res.write(': latido\n\n'), 20_000)
    req.raw.on('close', () => {
      clearInterval(latido)
      jobs.off('cambio', onCambio)
      jobs.off('linea', onLinea)
    })
  })

  // ------------------------------------------------------------- bibliotecas

  app.get('/api/v1/admin/bibliotecas', async () => listarBibliotecas(ctx.db))

  app.post('/api/v1/admin/bibliotecas', envolver(async (req, reply) => {
    const b = (req.body ?? {}) as { nombre?: unknown; tipo?: unknown; ruta?: unknown; escanear?: unknown }
    if (!(TIPOS_BIBLIOTECA as readonly unknown[]).includes(b.tipo)) throw new ErrorAdmin('Tipo de biblioteca inválido.')
    const id = crearBiblioteca(ctx.db, b)
    reply.code(201)
    return { id, job: b.escanear === false ? null : vista(escanear(biblioteca(id))) }
  }))

  app.put('/api/v1/admin/bibliotecas/:id', envolver(async (req) => {
    const id = (req.params as { id: string }).id
    const { rutaCambio } = actualizarBiblioteca(ctx.db, id, (req.body ?? {}) as { nombre?: unknown; ruta?: unknown })
    // Cambiar de carpeta dispara un escaneo: lo que se movió se reconcilia por hash sin perder progreso ni metadata.
    return { id, job: rutaCambio ? vista(escanear(biblioteca(id))) : null }
  }))

  app.delete('/api/v1/admin/bibliotecas/:id', envolver(async (req) => {
    const id = (req.params as { id: string }).id
    if (jobs.enCurso().some((j) => j.bibliotecaId === id)) throw new ErrorAdmin('Hay un trabajo en curso para esta biblioteca. Espera a que termine o cancélalo.', 409)
    eliminarBiblioteca(ctx.db, id, (req.query as { confirm?: unknown }).confirm)
    return { ok: true }
  }))

  // ------------------------------------------------------------- huérfanos y re-vinculación

  app.get('/api/v1/admin/bibliotecas/:id/huerfanos', envolver(async (req) => listarHuerfanos(ctx.db, (req.params as { id: string }).id)))

  app.delete('/api/v1/admin/bibliotecas/:id/huerfanos', envolver(async (req) => ({
    eliminados: eliminarHuerfanos(ctx.db, (req.params as { id: string }).id, (req.query as { confirm?: unknown }).confirm)
  })))

  app.get('/api/v1/admin/items/:id/archivo', envolver(async (req) => infoArchivo(ctx.db, (req.params as { id: string }).id)))

  app.post('/api/v1/admin/items/:id/revincular', envolver(async (req) => {
    await revincular(ctx.db, (req.params as { id: string }).id, (req.body as { rutaRelativa?: unknown } | null)?.rutaRelativa)
    return { ok: true }
  }))

  /** Sin `aplicar` solo previsualiza (nombre actual → nuevo); con `aplicar: true` renombra en el disco. */
  app.post('/api/v1/admin/items/:id/homologar', envolver(async (req) => {
    const b = (req.body ?? {}) as { titulo?: unknown; anio?: unknown; aplicar?: unknown }
    const id = (req.params as { id: string }).id
    if (b.aplicar === true) {
      const bib = ctx.db.prepare('SELECT library_id FROM media_items WHERE id = ?').get(id) as { library_id: string } | undefined
      if (bib && jobs.enCurso().some((j) => j.bibliotecaId === bib.library_id || j.bibliotecaId === null)) throw new ErrorAdmin('Hay un escaneo en curso: espera a que termine para renombrar.', 409)
    }
    return homologarNombre(ctx.db, id, b, b.aplicar === true)
  }))

  // ------------------------------------------------------------- metadata

  const tabla = (t: string): 'media_items' | 'series' => (t === 'series' ? 'series' : 'media_items')
  const existe = (t: 'media_items' | 'series', id: string): void => {
    if (!ctx.db.prepare(`SELECT 1 FROM ${t} WHERE id = ?`).get(id)) throw new ErrorAdmin('No existe.', 404)
  }

  app.put('/api/v1/admin/:coleccion/:id/metadata', envolver(async (req) => {
    const { coleccion, id } = req.params as { coleccion: string; id: string }
    if (coleccion !== 'items' && coleccion !== 'series') throw new ErrorAdmin('Ruta no encontrada.', 404)
    existe(tabla(coleccion), id)
    editarMetadata(ctx.db, tabla(coleccion), id, (req.body ?? {}) as EdicionManual)
    return { ok: true }
  }))

  app.get('/api/v1/admin/tmdb/buscar', envolver(async (req) => {
    const q = req.query as { tipo?: string; q?: string; anio?: string }
    if (!(TIPOS_BIBLIOTECA as readonly unknown[]).includes(q.tipo)) throw new ErrorAdmin('tipo inválido.')
    const anio = q.anio ? Number.parseInt(q.anio, 10) : null
    return tmdb.buscar(claveTmdb(), q.tipo as TipoBiblioteca, q.q ?? '', Number.isNaN(anio) ? null : anio)
  }))

  app.post('/api/v1/admin/:coleccion/:id/tmdb', envolver(async (req) => {
    const { coleccion, id } = req.params as { coleccion: string; id: string }
    if (coleccion !== 'items' && coleccion !== 'series') throw new ErrorAdmin('Ruta no encontrada.', 404)
    const tmdbId = Number((req.body as { tmdbId?: unknown } | null)?.tmdbId)
    if (!Number.isInteger(tmdbId) || tmdbId <= 0) throw new ErrorAdmin('tmdbId inválido.')
    existe(tabla(coleccion), id)
    await asignarManual(ctx.db, tmdb, claveTmdb(), { tipo: coleccion === 'series' ? 'serie' : 'item', id }, tmdbId)
    return { ok: true }
  }))

  // ------------------------------------------------------------- ajustes

  /** Estilo de subtítulos compartido con las otras versiones (claves heredadas): tamaño y separación del borde inferior, en las unidades de Flutter. */
  const leerNum = (clave: string, defecto: number): number => {
    const n = Number(ctx.auth.ajuste(clave))
    return Number.isFinite(n) && n > 0 ? n : defecto
  }
  app.get('/api/v1/admin/ajustes/subtitulos', async () => ({ tamano: leerNum('subtitle_font_size', 32), posicion: leerNum('subtitle_vertical_offset', 24) }))
  app.put('/api/v1/admin/ajustes/subtitulos', envolver(async (req) => {
    const b = (req.body ?? {}) as { tamano?: unknown; posicion?: unknown }
    const ok = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v) && v >= 0 && v <= 500
    if (b.tamano !== undefined) { if (!ok(b.tamano)) throw new ErrorAdmin('tamano inválido.'); ctx.auth.guardarAjuste('subtitle_font_size', String(b.tamano)) }
    if (b.posicion !== undefined) { if (!ok(b.posicion)) throw new ErrorAdmin('posicion inválida.'); ctx.auth.guardarAjuste('subtitle_vertical_offset', String(b.posicion)) }
    return { tamano: leerNum('subtitle_font_size', 32), posicion: leerNum('subtitle_vertical_offset', 24) }
  }))

  app.get('/api/v1/admin/ajustes/tmdb', async () => {
    const k = ctx.auth.ajuste(CLAVE_TMDB)
    return { configurada: !!k?.trim(), mascara: enmascarar(k?.trim() ?? null) }
  })

  app.put('/api/v1/admin/ajustes/tmdb', envolver(async (req) => {
    const k = (req.body as { apiKey?: unknown } | null)?.apiKey
    if (k !== null && typeof k !== 'string') throw new ErrorAdmin('apiKey inválida.')
    const limpia = typeof k === 'string' ? k.trim() : null
    ctx.auth.guardarAjuste(CLAVE_TMDB, limpia || null)
    return { configurada: !!limpia, mascara: enmascarar(limpia || null) }
  }))

  /** Prueba la clave enviada o, si no se manda, la guardada. */
  app.post('/api/v1/admin/ajustes/tmdb/probar', envolver(async (req) => {
    const k = (req.body as { apiKey?: unknown } | null)?.apiKey
    const clave = typeof k === 'string' && k.trim() ? k.trim() : claveTmdb()
    await tmdb.probarClave(clave)
    return { ok: true }
  }))

  // ------------------------------------------------------------- datos (caché y temporales)

  /** Tamaños de lo que CinemaLove guarda en la carpeta de datos. */
  app.get('/api/v1/admin/datos', envolver(async () => {
    if (!ctx.carpetaDatos) throw new ErrorAdmin('No hay una carpeta de datos abierta.')
    return infoDatos(ctx.carpetaDatos)
  }))

  /** Borra la caché de imágenes de CinemaLove (se vuelven a bajar de TMDb) o los temporales de reproducción. */
  app.post<{ Params: { que: string } }>('/api/v1/admin/datos/limpiar/:que', envolver(async (req) => {
    if (!ctx.carpetaDatos) throw new ErrorAdmin('No hay una carpeta de datos abierta.')
    if (req.params.que === 'imagenes') return limpiarImagenes(ctx.soloLectura ? null : ctx.db, ctx.carpetaDatos)
    if (req.params.que === 'temporales') return limpiarTemporales(ctx.carpetaDatos)
    throw new ErrorAdmin('Solo se puede limpiar «imagenes» o «temporales».')
  }))

  // ------------------------------------------------------------- transcodificación (aceleración por hardware)

  const ALTURAS_VALIDAS = [0, 480, 720, 1080, 1440, 2160]
  const HW_VALIDOS = ['off', 'auto', 'nvenc', 'qsv', 'amf', 'vaapi']
  const vistaTranscodificacion = (): AjustesTranscodificacion => ({
    disponible: !!ctx.reproduccion,
    hw: (HW_VALIDOS.includes(ctx.auth.ajuste('cl.hw_accel') ?? '') ? ctx.auth.ajuste('cl.hw_accel') : 'off') as AjustesTranscodificacion['hw'],
    maxTranscodes: Math.max(1, Number(ctx.auth.ajuste('cl.max_transcodes')) || 2),
    maxAltura: ALTURAS_VALIDAS.includes(Number(ctx.auth.ajuste('cl.max_altura'))) ? Number(ctx.auth.ajuste('cl.max_altura')) : 0,
    prueba: ctx.reproduccion?.playback.ultimaPruebaHw ?? null
  })

  app.get('/api/v1/admin/transcodificacion', envolver(async () => vistaTranscodificacion()))

  app.put('/api/v1/admin/transcodificacion', envolver(async (req) => {
    if (ctx.soloLectura) throw new ErrorAdmin(SOLO_LECTURA, 409)
    const b = (req.body ?? {}) as { hw?: unknown; maxTranscodes?: unknown; maxAltura?: unknown }
    if (b.hw !== undefined) {
      if (typeof b.hw !== 'string' || !HW_VALIDOS.includes(b.hw)) throw new ErrorAdmin('Aceleración no válida.')
      ctx.auth.guardarAjuste('cl.hw_accel', b.hw)
    }
    if (b.maxTranscodes !== undefined) {
      const n = Number(b.maxTranscodes)
      if (!Number.isInteger(n) || n < 1 || n > 8) throw new ErrorAdmin('Las conversiones simultáneas deben ser entre 1 y 8.')
      ctx.auth.guardarAjuste('cl.max_transcodes', String(n))
    }
    if (b.maxAltura !== undefined) {
      if (typeof b.maxAltura !== 'number' || !ALTURAS_VALIDAS.includes(b.maxAltura)) throw new ErrorAdmin('Calidad máxima no válida.')
      ctx.auth.guardarAjuste('cl.max_altura', String(b.maxAltura))
    }
    return vistaTranscodificacion()
  }))

  /** «Probar mi equipo»: una codificación real de 1 s por cada encoder de hardware que trae ffmpeg. Tarda unos segundos. */
  app.post('/api/v1/admin/transcodificacion/probar', envolver(async () => {
    if (!ctx.reproduccion) throw new ErrorAdmin('Esta instalación no incluye ffmpeg.', 409)
    await ctx.reproduccion.playback.probarAceleracion(true)
    return vistaTranscodificacion()
  }))

  rutasAdminServidor(app, ctx, envolver)
}
