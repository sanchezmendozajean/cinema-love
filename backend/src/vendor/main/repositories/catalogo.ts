import type Database from 'better-sqlite3'
import type {
  Destacado,
  ElementoCatalogo,
  ElementoContinuar,
  Facetas,
  FiltrosCatalogo,
  Home,
  OrdenCatalogo,
  ProgresoResumen,
  RespuestaCatalogo
} from '../../shared/api-types'
import type { EstadoVisto } from '../../shared/enums'
import { estadoVistoDesdeDb, generosDesdeDb, tipoBibliotecaDesdeDb } from '../db/codecs'
import { normalizarTexto } from '../db/texto'

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type Fila = Record<string, any>
type Params = Record<string, string | number | null>

/** Progreso de un ítem tal como lo guarda el cliente con token en su equipo. */
export interface EstadoExterno {
  id: string
  estado: EstadoVisto
  posicionSegundos: number
  ultima: number | null
}

const TAMANO_MAX = 200

/** Películas (series_id nulo) + series visibles, con el progreso del principal ya unido. */
/** `texto`: normaliza títulos (JS por fila, caro); `ultima`: última reproducción de cada serie (subconsulta). */
const baseCte = ({ texto, ultima }: { texto: boolean; ultima: boolean }): string => `
WITH base AS (
  SELECT 'pelicula' AS kind, m.id, m.library_id, m.titulo, m.titulo_original, ${texto ? 'unaccent_lower(COALESCE(m.titulo, nombre_archivo(m.ruta_relativa)))' : 'NULL'} AS tn, ${texto ? 'unaccent_lower(m.titulo_original)' : 'NULL'} AS tno, m.anio, m.poster_url,
         CASE WHEN m.generos IS NOT NULL AND json_valid(m.generos) THEN m.generos ELSE '[]' END AS generos,
         m.duracion_segundos AS duracion, m.creado_en,
         wp.estado AS p_estado, wp.posicion_segundos AS p_pos, wp.ultima_reproduccion AS ultima,
         CASE WHEN m.titulo IS NULL THEN nombre_archivo(m.ruta_relativa) END AS titulo_archivo
    FROM media_items m
    LEFT JOIN watch_progresses wp ON wp.media_item_id = m.id AND wp.usuario_id IS @uid
   WHERE m.series_id IS NULL AND m.estado_archivo = 'ok'
  UNION ALL
  SELECT 'serie', s.id, s.library_id, s.titulo, s.titulo_original, ${texto ? 'unaccent_lower(COALESCE(s.titulo, nombre_archivo(s.ruta_relativa)))' : 'NULL'}, ${texto ? 'unaccent_lower(s.titulo_original)' : 'NULL'}, s.anio, s.poster_url,
         CASE WHEN s.generos IS NOT NULL AND json_valid(s.generos) THEN s.generos ELSE '[]' END,
         NULL, s.creado_en, NULL, NULL,
         ${
           ultima
             ? `(SELECT MAX(wp.ultima_reproduccion) FROM media_items e
                  JOIN watch_progresses wp ON wp.media_item_id = e.id AND wp.usuario_id IS @uid
                 WHERE e.series_id = s.id)`
             : 'NULL'
         },
         CASE WHEN s.titulo IS NULL THEN nombre_archivo(s.ruta_relativa) END
    FROM series s
   WHERE s.estado_archivo = 'ok'
)`

type Filtro = 'q' | 'generos' | 'bibliotecas' | 'tipo' | 'anio' | 'estado'

const escaparLike = (s: string): string => s.replace(/[\\%_]/g, (c) => `\\${c}`)

/** WHERE combinado con AND; `excluir` quita un filtro (patrón de facetas). */
function construirWhere(f: FiltrosCatalogo, excluir?: Filtro): { sql: string; params: Params } {
  const partes: string[] = []
  const params: Params = {}

  if (excluir !== 'q' && f.q) {
    normalizarTexto(f.q)
      .split(/\s+/)
      .filter(Boolean)
      .forEach((t, i) => {
        params[`q${i}`] = `%${escaparLike(t)}%`
        partes.push(
          `(b.tn LIKE @q${i} ESCAPE '\\' OR b.tno LIKE @q${i} ESCAPE '\\')`
        )
      })
  }
  if (excluir !== 'generos' && f.generos?.length) {
    const ph = f.generos.map((g, i) => {
      params[`g${i}`] = g
      return `@g${i}`
    })
    partes.push(`EXISTS (SELECT 1 FROM json_each(b.generos) j WHERE j.value IN (${ph.join(',')}))`)
  }
  if (excluir !== 'bibliotecas' && f.bibliotecas?.length) {
    const ph = f.bibliotecas.map((g, i) => {
      params[`b${i}`] = g
      return `@b${i}`
    })
    partes.push(`b.library_id IN (${ph.join(',')})`)
  }
  if (excluir !== 'tipo' && f.tipo) {
    params.tipo = f.tipo
    partes.push('b.kind = @tipo')
  }
  if (excluir !== 'anio') {
    if (f.anioDesde != null) {
      params.anioDesde = f.anioDesde
      partes.push('b.anio >= @anioDesde')
    }
    if (f.anioHasta != null) {
      params.anioHasta = f.anioHasta
      partes.push('b.anio <= @anioHasta')
    }
  }
  // El estado de visto solo aplica a películas: con este filtro las series quedan fuera.
  if (excluir !== 'estado' && f.estado) {
    params.estado = f.estado
    partes.push("b.kind = 'pelicula' AND COALESCE(b.p_estado, 'noVisto') = @estado")
  }
  return { sql: partes.length ? `WHERE ${partes.join(' AND ')}` : '', params }
}

const CLAVES_ORDEN: Record<string, string> = {
  titulo: "COALESCE(b.tn, '')",
  anio: 'b.anio',
  agregado: 'b.creado_en',
  reproducido: 'b.ultima'
}

function construirOrden(orden: OrdenCatalogo | undefined): string {
  const o = orden ?? 'titulo'
  const desc = o.startsWith('-')
  const clave = CLAVES_ORDEN[desc ? o.slice(1) : o] ?? CLAVES_ORDEN.titulo
  // Los nulos siempre al final, y el título desempata para que la paginación sea estable.
  return `(${clave} IS NULL), ${clave} ${desc ? 'DESC' : 'ASC'}, COALESCE(b.tn, ''), b.id`
}

/** Ítems sin metadatos de TMDb: se ofrece el nombre derivado del archivo (la ruta real no se expone). */
const sinMetadatos = (r: Fila): { sinMetadatos?: true; tituloArchivo?: string } =>
  r.titulo == null && r.titulo_archivo ? { sinMetadatos: true, tituloArchivo: r.titulo_archivo } : {}

const progresoDe = (estado: string | null, pos: number | null): ProgresoResumen | null =>
  estado ? { estado: estadoVistoDesdeDb(estado), posicionSegundos: pos ?? 0 } : null

export const etiquetaEpisodio = (t: number | null, e: number | null): string =>
  `S${String(t ?? 0).padStart(2, '0')}E${String(e ?? 0).padStart(2, '0')}`

/** Fila de ítem (+ su serie) → tarjeta de «continuar viendo»: película o episodio. */
function aContinuar(r: Fila, progreso: ElementoContinuar['progreso']): ElementoContinuar {
  if (!r.series_id) {
    return { kind: 'pelicula', id: r.id, titulo: r.titulo, posterUrl: r.poster_url, backdropUrl: r.backdrop_url, duracionSegundos: r.duracion_segundos, progreso }
  }
  return {
    kind: 'episodio',
    id: r.id,
    titulo: r.titulo,
    posterUrl: r.poster_url ?? r.s_poster,
    backdropUrl: r.backdrop_url ?? r.s_backdrop,
    duracionSegundos: r.duracion_segundos,
    progreso,
    serie: { id: r.series_id, titulo: r.s_titulo, posterUrl: r.s_poster },
    episodio: etiquetaEpisodio(r.numero_temporada, r.numero_episodio)
  }
}

type EstadisticasSerie = { temporadas: number; episodios: number; episodiosVistos: number }

/** Fila de la CTE base → elemento del catálogo (película o serie). */
function aElemento(r: Fila, series: Map<string, EstadisticasSerie>): ElementoCatalogo {
  const comun = {
    id: r.id as string,
    libraryId: r.library_id as string,
    titulo: r.titulo as string | null,
    ...sinMetadatos(r),
    anio: r.anio as number | null,
    posterUrl: r.poster_url as string | null,
    generos: generosDesdeDb(r.generos)
  }
  return r.kind === 'pelicula'
    ? { kind: 'pelicula', ...comun, duracionSegundos: r.duracion, progreso: progresoDe(r.p_estado, r.p_pos) }
    : { kind: 'serie', ...comun, ...(series.get(r.id) ?? { temporadas: 0, episodios: 0, episodiosVistos: 0 }) }
}

/** Consultas del catálogo unificado y de Inicio. `usuarioId` null = admin local. */
export class CatalogoRepo {
  constructor(private readonly db: Database.Database) {}

  private todos(sql: string, params: Params): Fila[] {
    return this.db.prepare(sql).all(params) as Fila[]
  }

  /**
   * Ejecuta `fn` (consultas de este repositorio con `usuarioId` null) viendo como progreso el que manda el cliente en vez del
   * guardado en el servidor. Se hace con una tabla TEMP llamada igual que la real, que la tapa SOLO mientras dura `fn`: tiene que
   * ser síncrona (una sola conexión SQLite: ninguna otra petición puede colarse entre crear y borrar la tabla).
   */
  conEstadosExternos<T>(estados: EstadoExterno[], fn: () => T): T {
    this.db.exec(
      `CREATE TEMP TABLE watch_progresses (id INTEGER PRIMARY KEY AUTOINCREMENT, media_item_id TEXT NOT NULL, usuario_id TEXT NULL,
         posicion_segundos INTEGER NOT NULL DEFAULT 0, estado TEXT NOT NULL, ultima_reproduccion INTEGER NULL)`
    )
    try {
      const ins = this.db.prepare('INSERT INTO temp.watch_progresses (media_item_id, posicion_segundos, estado, ultima_reproduccion) VALUES (?, ?, ?, ?)')
      this.db.transaction(() => {
        // De la más antigua a la más reciente: el `id` desempata el orden de «continuar viendo».
        for (const e of [...estados].sort((a, b) => (a.ultima ?? 0) - (b.ultima ?? 0))) ins.run(e.id, e.posicionSegundos, e.estado, e.ultima)
      })()
      return fn()
    } finally {
      this.db.exec('DROP TABLE temp.watch_progresses')
    }
  }

  buscar(f: FiltrosCatalogo, usuarioId: string | null): RespuestaCatalogo {
    const tamano = Math.min(Math.max(Math.trunc(f.tamano ?? 60), 1), TAMANO_MAX)
    const pagina = Math.max(Math.trunc(f.pagina ?? 1), 1)
    const base: Params = { uid: usuarioId }
    const ordenPor = (f.orden ?? 'titulo').replace(/^-/, '')
    const cteConteo = baseCte({ texto: !!f.q, ultima: false })
    const ctePagina = baseCte({ texto: !!f.q || ordenPor === 'titulo', ultima: ordenPor === 'reproducido' })

    const w = construirWhere(f)
    const total = (
      this.db.prepare(`${cteConteo} SELECT COUNT(*) AS n FROM base b ${w.sql}`).get({ ...base, ...w.params }) as {
        n: number
      }
    ).n

    const filas = this.todos(
      `${ctePagina} SELECT b.* FROM base b ${w.sql} ORDER BY ${construirOrden(f.orden)} LIMIT @limite OFFSET @desde`,
      { ...base, ...w.params, limite: tamano, desde: (pagina - 1) * tamano }
    )

    const series = this.estadisticasDeSeries(
      filas.filter((r) => r.kind === 'serie').map((r) => r.id as string),
      usuarioId
    )
    const elementos: ElementoCatalogo[] = filas.map((r) => aElemento(r, series))

    return { total, pagina, tamano, elementos, facetas: this.facetas(f, base, baseCte({ texto: !!f.q, ultima: false })) }
  }

  private estadisticasDeSeries(
    ids: string[],
    usuarioId: string | null
  ): Map<string, { temporadas: number; episodios: number; episodiosVistos: number }> {
    const mapa = new Map<string, { temporadas: number; episodios: number; episodiosVistos: number }>()
    if (!ids.length) return mapa
    const params: Params = { uid: usuarioId }
    const ph = ids.map((id, i) => {
      params[`s${i}`] = id
      return `@s${i}`
    })
    const filas = this.todos(
      `SELECT e.series_id AS id, COUNT(DISTINCT e.numero_temporada) AS temporadas, COUNT(*) AS episodios,
              SUM(CASE WHEN wp.estado = 'visto' THEN 1 ELSE 0 END) AS vistos
         FROM media_items e
         LEFT JOIN watch_progresses wp ON wp.media_item_id = e.id AND wp.usuario_id IS @uid
        WHERE e.series_id IN (${ph.join(',')}) AND e.estado_archivo = 'ok'
        GROUP BY e.series_id`,
      params
    )
    for (const r of filas)
      mapa.set(r.id, { temporadas: r.temporadas, episodios: r.episodios, episodiosVistos: r.vistos ?? 0 })
    return mapa
  }

  /** Cada faceta se calcula sobre el resultado filtrado SIN su propio filtro. */
  private facetas(f: FiltrosCatalogo, base: Params, cte: string): Facetas {
    const wg = construirWhere(f, 'generos')
    const generos = this.todos(
      `${cte} SELECT j.value AS valor, COUNT(*) AS conteo FROM base b, json_each(b.generos) j
        ${wg.sql} GROUP BY j.value ORDER BY conteo DESC, valor COLLATE NOCASE`,
      { ...base, ...wg.params }
    ).map((r) => ({ valor: String(r.valor), conteo: r.conteo as number }))

    const wb = construirWhere(f, 'bibliotecas')
    const conteos = new Map(
      this.todos(`${cte} SELECT b.library_id AS id, COUNT(*) AS conteo FROM base b ${wb.sql} GROUP BY b.library_id`, {
        ...base,
        ...wb.params
      }).map((r) => [r.id as string, r.conteo as number])
    )
    const bibliotecas = this.todos('SELECT id, nombre, tipo FROM libraries ORDER BY nombre COLLATE NOCASE', {}).map(
      (l) => ({ id: l.id, nombre: l.nombre, tipo: tipoBibliotecaDesdeDb(l.tipo), conteo: conteos.get(l.id) ?? 0 })
    )

    const wa = construirWhere(f, 'anio')
    const rango = this.db
      .prepare(`${cte} SELECT MIN(b.anio) AS min, MAX(b.anio) AS max FROM base b ${wa.sql}`)
      .get({ ...base, ...wa.params }) as { min: number | null; max: number | null }

    return { generos, bibliotecas, anios: { min: rango.min, max: rango.max } }
  }

  /** Datos de las tarjetas de Bibliotecas: cuántos títulos, cuántos sin metadatos y 3 backdrops para el collage. */
  resumenBibliotecas(): Map<string, { conteo: number; sinMetadatos: number; backdrops: string[] }> {
    const mapa = new Map<string, { conteo: number; sinMetadatos: number; backdrops: string[] }>()
    const filas = this.todos(
      `SELECT library_id AS id, COUNT(*) AS conteo, SUM(titulo IS NULL) AS sin FROM (
         SELECT library_id, titulo FROM media_items WHERE series_id IS NULL AND estado_archivo = 'ok'
         UNION ALL SELECT library_id, titulo FROM series WHERE estado_archivo = 'ok'
       ) GROUP BY library_id`,
      {}
    )
    for (const f of filas) mapa.set(f.id, { conteo: f.conteo, sinMetadatos: f.sin ?? 0, backdrops: [] })
    for (const [id, r] of mapa) {
      r.backdrops = this.todos(
        `SELECT backdrop_url FROM (
           SELECT backdrop_url, creado_en FROM media_items WHERE library_id = @id AND series_id IS NULL AND estado_archivo = 'ok' AND backdrop_url IS NOT NULL
           UNION ALL SELECT backdrop_url, creado_en FROM series WHERE library_id = @id AND estado_archivo = 'ok' AND backdrop_url IS NOT NULL
         ) ORDER BY creado_en DESC LIMIT 3`,
        { id }
      ).map((x) => x.backdrop_url as string)
    }
    return mapa
  }

  // --- Inicio

  home(usuarioId: string | null): Home {
    const continuarViendo = this.continuarViendo(usuarioId)
    const destacados = this.destacados(usuarioId, continuarViendo)
    return {
      continuarViendo,
      proximasAVer: this.proximasAVer(usuarioId),
      agregadosRecientemente: this.buscar({ orden: '-agregado', tamano: 20 }, usuarioId).elementos,
      vistosRecientemente: this.vistosRecientemente(usuarioId),
      destacados,
      destacado: destacados[0] ?? null
    }
  }

  private continuarViendo(usuarioId: string | null, limite = 20): ElementoContinuar[] {
    const filas = this.todos(
      `SELECT m.id, m.titulo, m.poster_url, m.backdrop_url, m.duracion_segundos, m.series_id,
              m.numero_temporada, m.numero_episodio, wp.posicion_segundos AS pos, wp.estado,
              s.titulo AS s_titulo, s.poster_url AS s_poster, s.backdrop_url AS s_backdrop
         FROM watch_progresses wp
         JOIN media_items m ON m.id = wp.media_item_id AND m.estado_archivo = 'ok'
         LEFT JOIN series s ON s.id = m.series_id
        WHERE wp.usuario_id IS @uid AND wp.estado = 'enProgreso'
        ORDER BY wp.ultima_reproduccion DESC, wp.id DESC LIMIT @limite`,
      { uid: usuarioId, limite }
    )
    return filas.map((r) => aContinuar(r, { estado: 'enProgreso' as EstadoVisto, posicionSegundos: r.pos as number }))
  }

  /**
   * Datos de tarjeta «continuar viendo» de ítems concretos (película o episodio), en el orden pedido y con progreso en cero:
   * el cliente con token pone el suyo, que vive en su equipo.
   */
  resumenesContinuar(ids: string[]): ElementoContinuar[] {
    if (!ids.length) return []
    const params: Params = {}
    const ph = ids.map((id, i) => {
      params[`i${i}`] = id
      return `@i${i}`
    })
    const filas = this.todos(
      `SELECT m.id, m.titulo, m.poster_url, m.backdrop_url, m.duracion_segundos, m.series_id,
              m.numero_temporada, m.numero_episodio, 0 AS pos,
              s.titulo AS s_titulo, s.poster_url AS s_poster, s.backdrop_url AS s_backdrop
         FROM media_items m LEFT JOIN series s ON s.id = m.series_id
        WHERE m.estado_archivo = 'ok' AND m.id IN (${ph.join(',')})`,
      params
    )
    const porId = new Map(filas.map((r) => [r.id as string, aContinuar(r, { estado: 'noVisto' as EstadoVisto, posicionSegundos: 0 })]))
    return ids.flatMap((id) => porId.get(id) ?? [])
  }

  /** Próximas a ver y vistos recientemente: películas (los episodios sueltos no son tarjetas del catálogo). */
  private proximasAVer(usuarioId: string | null, limite = 30): ElementoCatalogo[] {
    return this.elementosPorIds(
      this.todos(
        `SELECT p.media_item_id AS id FROM proximas_a_ver p WHERE p.usuario_id IS @uid
          ORDER BY p.agregado_en DESC, p.id DESC LIMIT @limite`,
        { uid: usuarioId, limite }
      ).map((r) => r.id),
      usuarioId
    )
  }

  private vistosRecientemente(usuarioId: string | null, limite = 20): ElementoCatalogo[] {
    return this.elementosPorIds(
      this.todos(
        `SELECT wp.media_item_id AS id FROM watch_progresses wp
          WHERE wp.usuario_id IS @uid AND wp.estado = 'visto'
          ORDER BY wp.ultima_reproduccion DESC, wp.id DESC LIMIT @limite`,
        { uid: usuarioId, limite: limite * 3 }
      ).map((r) => r.id),
      usuarioId
    ).slice(0, limite)
  }

  /** Mantiene el orden de `ids` y descarta lo que no sea una película visible. */
  elementosPorIds(ids: string[], usuarioId: string | null): ElementoCatalogo[] {
    return this.porIds(ids, usuarioId, true)
  }

  /** Igual, pero admite también series (p. ej. "en tu colección" de una persona). */
  elementosPorIdsCualquiera(ids: string[], usuarioId: string | null): ElementoCatalogo[] {
    return this.porIds(ids, usuarioId, false)
  }

  private porIds(ids: string[], usuarioId: string | null, soloPeliculas: boolean): ElementoCatalogo[] {
    if (!ids.length) return []
    const params: Params = { uid: usuarioId }
    const ph = ids.map((id, i) => {
      params[`i${i}`] = id
      return `@i${i}`
    })
    const cte = baseCte({ texto: false, ultima: false })
    const filas = this.todos(
      `${cte} SELECT b.* FROM base b WHERE ${soloPeliculas ? "b.kind = 'pelicula' AND " : ''}b.id IN (${ph.join(',')})`,
      params
    )
    const series = this.estadisticasDeSeries(filas.filter((r) => r.kind === 'serie').map((r) => r.id as string), usuarioId)
    const porId = new Map(filas.map((r) => [r.id as string, aElemento(r, series)]))
    return ids.flatMap((id) => porId.get(id) ?? [])
  }

  /**
   * Héroe de Inicio (hasta 5 destacados): primero lo que está en progreso, luego lo agregado más
   * recientemente que no se haya visto; todos con backdrop.
   */
  private destacados(usuarioId: string | null, continuar: ElementoContinuar[], max = 5): Destacado[] {
    const out: Destacado[] = []
    const usados = new Set<string>()
    const agregar = (d: Destacado | null): void => {
      if (d && !usados.has(d.id) && (d.serie ? !usados.has(d.serie.id) : true)) {
        usados.add(d.id)
        if (d.serie) usados.add(d.serie.id)
        out.push(d)
      }
    }

    for (const c of continuar) {
      if (out.length >= 3 || !c.backdropUrl) continue
      const r = this.db.prepare('SELECT anio, sinopsis, generos FROM media_items WHERE id = ?').get(c.id) as Fila | undefined
      if (!r) continue
      agregar({
        kind: c.kind,
        id: c.id,
        titulo: c.serie?.titulo ?? c.titulo,
        anio: r.anio,
        sinopsis: r.sinopsis,
        generos: generosDesdeDb(r.generos),
        posterUrl: c.posterUrl,
        backdropUrl: c.backdropUrl,
        duracionSegundos: c.duracionSegundos,
        progreso: c.progreso,
        ...(c.serie ? { serie: { id: c.serie.id, titulo: c.serie.titulo } } : {}),
        ...(c.episodio ? { episodio: c.episodio } : {})
      })
    }

    const recientes = this.todos(
      `SELECT m.id, m.titulo, m.anio, m.sinopsis, m.generos, m.poster_url, m.backdrop_url, m.duracion_segundos,
              wp.estado, wp.posicion_segundos AS pos
         FROM media_items m
         LEFT JOIN watch_progresses wp ON wp.media_item_id = m.id AND wp.usuario_id IS @uid
        WHERE m.series_id IS NULL AND m.estado_archivo = 'ok' AND m.backdrop_url IS NOT NULL
          AND COALESCE(wp.estado, 'noVisto') = 'noVisto'
        ORDER BY m.creado_en DESC, m.id LIMIT @limite`,
      { uid: usuarioId, limite: max * 2 }
    )
    for (const r of recientes) {
      if (out.length >= max) break
      agregar({
        kind: 'pelicula',
        id: r.id,
        titulo: r.titulo,
        anio: r.anio,
        sinopsis: r.sinopsis,
        generos: generosDesdeDb(r.generos),
        posterUrl: r.poster_url,
        backdropUrl: r.backdrop_url,
        duracionSegundos: r.duracion_segundos,
        progreso: progresoDe(r.estado, r.pos)
      })
    }
    return out
  }
}
