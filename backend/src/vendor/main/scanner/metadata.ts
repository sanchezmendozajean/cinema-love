import type Database from 'better-sqlite3'
import { randomUUID } from 'node:crypto'
import type { Persona } from '../db/codecs'
import { ahoraDb } from '../db/codecs'
import type { BibliotecaEscaneable } from './escaner'
import { parsearTitulo, type TituloParseado } from './nombres'
import { TmdbError, type DetalleTmdb, type TemporadaTmdb, type TmdbClient } from './tmdb-client'
import { elegirMejorCandidato, type ResultadoBusqueda } from './tmdb-match'

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type Fila = Record<string, any>

/** Reparto/equipo como los guarda Flutter: lista JSON (`[]` si está vacía) y sin las claves nulas. */
export function personasJson(p: Persona[]): string {
  return JSON.stringify(
    p.map((m) => ({
      ...(m.id != null ? { id: m.id } : {}),
      nombre: m.nombre,
      ...(m.rol != null ? { rol: m.rol } : {}),
      ...(m.fotoUrl != null ? { fotoUrl: m.fotoUrl } : {})
    }))
  )
}

// ------------------------------------------------------------------ guardado (mismas columnas que Flutter)

export function asignarMetadataItem(db: Database.Database, id: string, d: DetalleTmdb): void {
  db.prepare(
    `UPDATE media_items SET tmdb_id = ?, titulo = ?, titulo_original = ?, idioma_original = ?, anio = ?, sinopsis = ?, estudio = ?,
       poster_url = ?, backdrop_url = ?, duracion_segundos = ?, generos = ?, reparto = ?, equipo = ?, actualizado_en = ? WHERE id = ?`
  ).run(String(d.id), d.titulo, d.tituloOriginal, d.idiomaOriginal, d.anio, d.sinopsis, d.estudio, d.posterUrl, d.backdropUrl, d.duracionSegundos, JSON.stringify(d.generos), personasJson(d.reparto), personasJson(d.equipo), ahoraDb(), id)
}

export function asignarMetadataSerie(db: Database.Database, id: string, d: DetalleTmdb): void {
  db.prepare(
    `UPDATE series SET tmdb_id = ?, titulo = ?, titulo_original = ?, idioma_original = ?, anio = ?, sinopsis = ?, estudio = ?,
       poster_url = ?, backdrop_url = ?, generos = ?, reparto = ?, equipo = ?, actualizado_en = ? WHERE id = ?`
  ).run(String(d.id), d.titulo, d.tituloOriginal, d.idiomaOriginal, d.anio, d.sinopsis, d.estudio, d.posterUrl, d.backdropUrl, JSON.stringify(d.generos), personasJson(d.reparto), personasJson(d.equipo), ahoraDb(), id)
}

/**
 * Guarda la temporada (upsert por `(serie, número)`) y completa cada episodio de esa temporada cuyo número case con el
 * de TMDb; los que no casen (especiales, rips fuera de catálogo) quedan sin metadata. El still va en `poster_url`.
 */
export function asignarTemporada(db: Database.Database, serieId: string, numero: number, t: TemporadaTmdb): void {
  const ahora = ahoraDb()
  db.transaction(() => {
    const existente = db.prepare('SELECT id FROM seasons WHERE series_id = ? AND numero_temporada = ?').get(serieId, numero) as Fila | undefined
    if (existente) {
      db.prepare('UPDATE seasons SET tmdb_id = ?, titulo = ?, sinopsis = ?, poster_url = ?, reparto = ?, equipo = ?, actualizado_en = ? WHERE id = ?').run(
        t.tmdbId == null ? null : String(t.tmdbId), t.nombre, t.sinopsis, t.posterUrl, personasJson(t.reparto), personasJson(t.equipo), ahora, existente.id
      )
    } else {
      db.prepare('INSERT INTO seasons (id, series_id, numero_temporada, tmdb_id, titulo, sinopsis, poster_url, reparto, equipo, creado_en, actualizado_en) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)').run(
        randomUUID(), serieId, numero, t.tmdbId == null ? null : String(t.tmdbId), t.nombre, t.sinopsis, t.posterUrl, personasJson(t.reparto), personasJson(t.equipo), ahora, ahora
      )
    }
    const episodios = db.prepare("SELECT id, numero_episodio FROM media_items WHERE series_id = ? AND numero_temporada = ? AND estado_archivo = 'ok'").all(serieId, numero) as Fila[]
    const actualizar = db.prepare('UPDATE media_items SET tmdb_id = ?, titulo = ?, sinopsis = ?, poster_url = ?, duracion_segundos = ?, actualizado_en = ? WHERE id = ?')
    for (const e of t.episodios) {
      const ep = episodios.find((x) => x.numero_episodio === e.numeroEpisodio)
      if (ep) actualizar.run(e.tmdbId == null ? null : String(e.tmdbId), e.nombre, e.sinopsis, e.stillUrl, e.duracionSegundos, ahora, ep.id)
    }
  })()
}

/** Campos editables a mano (pestaña «Metadatos»): cualquier campo de metadata. Los ausentes no se tocan. */
export interface EdicionManual {
  titulo?: string | null
  tituloOriginal?: string | null
  idiomaOriginal?: string | null
  anio?: number | null
  sinopsis?: string | null
  estudio?: string | null
  posterUrl?: string | null
  backdropUrl?: string | null
  duracionSegundos?: number | null
  generos?: string[]
  reparto?: Persona[]
  equipo?: Persona[]
}

const COLUMNAS: Record<keyof EdicionManual, string> = {
  titulo: 'titulo',
  tituloOriginal: 'titulo_original',
  idiomaOriginal: 'idioma_original',
  anio: 'anio',
  sinopsis: 'sinopsis',
  estudio: 'estudio',
  posterUrl: 'poster_url',
  backdropUrl: 'backdrop_url',
  duracionSegundos: 'duracion_segundos',
  generos: 'generos',
  reparto: 'reparto',
  equipo: 'equipo'
}

/** Aplica una edición manual a una película/episodio (`media_items`) o a una serie (`series`, sin duración). */
export function editarMetadata(db: Database.Database, tabla: 'media_items' | 'series', id: string, e: EdicionManual): void {
  const sets: string[] = []
  const vals: unknown[] = []
  for (const [k, col] of Object.entries(COLUMNAS) as [keyof EdicionManual, string][]) {
    if (!(k in e) || e[k] === undefined) continue
    if (tabla === 'series' && k === 'duracionSegundos') continue
    sets.push(`${col} = ?`)
    const v = e[k]
    vals.push(k === 'generos' ? JSON.stringify(v ?? []) : k === 'reparto' || k === 'equipo' ? personasJson((v as Persona[]) ?? []) : typeof v === 'string' && v.trim() === '' ? null : v)
  }
  if (!sets.length) return
  db.prepare(`UPDATE ${tabla} SET ${sets.join(', ')}, actualizado_en = ? WHERE id = ?`).run(...vals, ahoraDb(), id)
}

// ------------------------------------------------------------------ importación masiva

export type EventoImport = { tipo: 'linea'; texto: string } | { tipo: 'progreso'; procesados: number; total: number; resueltos: number }

export interface Pendiente {
  id: string
  /** Ruta relativa del ítem/serie (para mostrarla al resolver a mano). */
  ruta: string
  /** Lo que se buscó, derivado del nombre de archivo/carpeta. */
  titulo: string
  anio?: number
  /** Para abrir su ficha: una película/episodio (`item`) o una serie. */
  tipo: 'item' | 'serie'
}

export interface ResumenImport {
  total: number
  resueltos: number
  /** Hubo varios candidatos y ninguno fue inequívoco → se resuelven uno a uno con el buscador manual. */
  ambiguos: Pendiente[]
  /** TMDb no devolvió nada. */
  sinCoincidencia: Pendiente[]
  cancelado: boolean
}

export interface OpcionesImport {
  soloFaltantes?: boolean
  senal?: AbortSignal
  onEvento?: (e: EventoImport) => void
}

const etiqueta = (p: TituloParseado): string => `${p.titulo}${p.anio != null ? ` (${p.anio})` : ''}`

/**
 * Asistente de metadatos de una biblioteca (specs/06 §5). La búsqueda se arma SIEMPRE desde el nombre de archivo o
 * carpeta, nunca desde el título guardado (así «volver a buscar todo» corrige errores en masa). Solo asigna cuando
 * `elegirMejorCandidato` encuentra una coincidencia inequívoca. Un error fatal de TMDb (clave, red) corta el trabajo.
 */
export async function importarMetadata(db: Database.Database, tmdb: TmdbClient, biblioteca: BibliotecaEscaneable, apiKey: string, o: OpcionesImport = {}): Promise<ResumenImport> {
  const emitir = o.onEvento ?? ((): void => undefined)
  const linea = (texto: string): void => emitir({ tipo: 'linea', texto })
  const soloFaltantes = o.soloFaltantes ?? true
  const esSerie = biblioteca.tipo === 'serie'

  const tabla = esSerie ? 'series' : 'media_items'
  const filtroMovies = esSerie ? '' : ' AND series_id IS NULL'
  const pendientes = db
    .prepare(`SELECT id, ruta_relativa FROM ${tabla} WHERE library_id = ? AND estado_archivo = 'ok'${filtroMovies}${soloFaltantes ? ' AND tmdb_id IS NULL' : ''} ORDER BY ruta_relativa`)
    .all(biblioteca.id) as { id: string; ruta_relativa: string }[]

  const resumen: ResumenImport = { total: pendientes.length, resueltos: 0, ambiguos: [], sinCoincidencia: [], cancelado: false }
  if (!pendientes.length) {
    linea(esSerie ? 'No hay series pendientes de metadata.' : 'No hay ítems pendientes de metadata.')
    return resumen
  }

  for (let i = 0; i < pendientes.length; i++) {
    if (o.senal?.aborted) {
      linea('— Cancelado por el usuario. —')
      resumen.cancelado = true
      return resumen
    }
    const item = pendientes[i]
    const parseado = parsearTitulo(item.ruta_relativa)
    const pendiente: Pendiente = { id: item.id, ruta: item.ruta_relativa, titulo: parseado.titulo, anio: parseado.anio, tipo: esSerie ? 'serie' : 'item' }
    linea(etiqueta(parseado))
    linea(`  Ubicación: ${item.ruta_relativa}`)

    try {
      linea('  Buscando en TMDb...')
      const resultados: ResultadoBusqueda[] = await tmdb.buscar(apiKey, biblioteca.tipo, parseado.titulo, parseado.anio)
      const elegido = elegirMejorCandidato(resultados, parseado)
      if (!elegido) {
        ;(resultados.length ? resumen.ambiguos : resumen.sinCoincidencia).push(pendiente)
        linea(resultados.length ? '  Varios candidatos, ninguno inequívoco — queda para revisión manual.' : '  Sin resultados en TMDb — queda para revisión manual.')
      } else {
        linea(`  Encontrado: "${elegido.titulo}" (${elegido.anio ?? '—'})`)
        linea(esSerie ? '  Descargando ficha de la serie...' : '  Descargando metadatos...')
        const detalle = await tmdb.obtenerDetalle(apiKey, biblioteca.tipo, elegido.id)
        if (esSerie) {
          asignarMetadataSerie(db, item.id, detalle)
          linea('  Ficha de la serie guardada. ✓')
          await importarTemporadas(db, tmdb, apiKey, item.id, elegido.id, linea, o.senal)
        } else {
          asignarMetadataItem(db, item.id, detalle)
          linea('  Guardado. ✓')
        }
        resumen.resueltos++
      }
    } catch (e) {
      if (e instanceof TmdbError && e.fatal) {
        linea(`  Error: ${e.message}`)
        emitir({ tipo: 'progreso', procesados: i + 1, total: pendientes.length, resueltos: resumen.resueltos })
        throw e // problema de red o de API key: no tiene sentido seguir intentando
      }
      linea(`  Error: ${e instanceof Error ? e.message : 'inesperado'} — se omite este ${esSerie ? 'serie' : 'ítem'}.`)
    }
    emitir({ tipo: 'progreso', procesados: i + 1, total: pendientes.length, resueltos: resumen.resueltos })
  }
  return resumen
}

/** Una petición por temporada presente en los episodios escaneados. */
export async function importarTemporadas(
  db: Database.Database,
  tmdb: TmdbClient,
  apiKey: string,
  serieId: string,
  tmdbSerieId: number,
  linea: (t: string) => void = () => undefined,
  senal?: AbortSignal
): Promise<void> {
  const numeros = (db.prepare('SELECT DISTINCT numero_temporada n FROM media_items WHERE series_id = ? AND numero_temporada IS NOT NULL ORDER BY n').all(serieId) as { n: number }[]).map((r) => r.n)
  for (const n of numeros) {
    if (senal?.aborted) return
    linea(`  Descargando temporada ${n}...`)
    try {
      const t = await tmdb.obtenerTemporada(apiKey, tmdbSerieId, n)
      asignarTemporada(db, serieId, n, t)
      linea(`  Temporada ${n}: ${t.episodios.length} episodio(s). ✓`)
    } catch (e) {
      if (e instanceof TmdbError && e.status === 401) throw e
      linea(`  Temporada ${n}: error (${e instanceof Error ? e.message : 'inesperado'}), se omite.`)
    }
  }
}

/** Asigna a mano el resultado elegido en el buscador (película/episodio suelto o serie completa). */
export async function asignarManual(
  db: Database.Database,
  tmdb: TmdbClient,
  apiKey: string,
  destino: { tipo: 'item' | 'serie'; id: string },
  tmdbId: number,
  linea: (t: string) => void = () => undefined
): Promise<DetalleTmdb> {
  if (destino.tipo === 'serie') {
    linea('Descargando ficha de la serie...')
    const d = await tmdb.obtenerDetalle(apiKey, 'serie', tmdbId)
    asignarMetadataSerie(db, destino.id, d)
    await importarTemporadas(db, tmdb, apiKey, destino.id, tmdbId, linea)
    return d
  }
  linea('Descargando metadatos...')
  const d = await tmdb.obtenerDetalle(apiKey, 'pelicula', tmdbId)
  asignarMetadataItem(db, destino.id, d)
  return d
}
