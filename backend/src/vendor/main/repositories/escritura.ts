import type Database from 'better-sqlite3'
import { randomUUID } from 'node:crypto'
import { ahoraDb, boolADb, boolDesdeDb, estadoVistoDesdeDb } from '../db/codecs'
import type { EstadoVisto } from '../../shared/enums'

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type Fila = Record<string, any>

export interface ProgresoDb {
  posicionSegundos: number
  estado: EstadoVisto
  ultimaReproduccion: number | null
  pistaAudioId: string | null
  pistaSubtituloId: string | null
}

export interface ColeccionDb {
  id: string
  nombre: string
  quitarAlVerse: boolean
  creadoEn: number
  /** Cantidad de ítems visibles y hasta 4 pósters para el mosaico. */
  conteo: number
  posters: string[]
}

const aProgreso = (r: Fila): ProgresoDb => ({
  posicionSegundos: r.posicion_segundos,
  estado: estadoVistoDesdeDb(r.estado),
  ultimaReproduccion: r.ultima_reproduccion,
  pistaAudioId: r.pista_audio_id,
  pistaSubtituloId: r.pista_subtitulo_id
})

/**
 * Escrituras sobre las tablas heredadas, con la semántica exacta de Flutter (specs/02 §4):
 * una fila por (ítem, usuario) garantizada aquí porque el esquema no tiene UNIQUE, y `usuario_id IS ?`
 * (NULL = admin local, también para la API key).
 */
export class Escritura {
  constructor(private readonly db: Database.Database) {}

  // --- progreso global

  obtenerProgreso(mediaItemId: string, usuarioId: string | null): ProgresoDb | null {
    const r = this.db
      .prepare('SELECT * FROM watch_progresses WHERE media_item_id = ? AND usuario_id IS ?')
      .get(mediaItemId, usuarioId) as Fila | undefined
    return r ? aProgreso(r) : null
  }

  /** Upsert conservando las pistas elegidas si no se mandan nuevas. */
  guardarProgreso(
    mediaItemId: string,
    usuarioId: string | null,
    posicionSegundos: number,
    estado: EstadoVisto,
    pistaAudioId?: string | null,
    pistaSubtituloId?: string | null
  ): void {
    this.db.transaction(() => {
      const existente = this.db
        .prepare('SELECT id, pista_audio_id, pista_subtitulo_id FROM watch_progresses WHERE media_item_id = ? AND usuario_id IS ?')
        .get(mediaItemId, usuarioId) as Fila | undefined
      const ahora = ahoraDb()
      if (!existente) {
        this.db
          .prepare(
            `INSERT INTO watch_progresses (media_item_id, usuario_id, posicion_segundos, estado, ultima_reproduccion, pista_audio_id, pista_subtitulo_id)
             VALUES (?, ?, ?, ?, ?, ?, ?)`
          )
          .run(mediaItemId, usuarioId, posicionSegundos, estado, ahora, pistaAudioId ?? null, pistaSubtituloId ?? null)
      } else {
        this.db
          .prepare(
            `UPDATE watch_progresses SET posicion_segundos = ?, estado = ?, ultima_reproduccion = ?,
                    pista_audio_id = ?, pista_subtitulo_id = ? WHERE id = ?`
          )
          .run(
            posicionSegundos,
            estado,
            ahora,
            pistaAudioId ?? existente.pista_audio_id,
            pistaSubtituloId ?? existente.pista_subtitulo_id,
            existente.id
          )
      }
    })()
  }

  /** Visto: posición 0 y se retira de Próximas a ver. */
  marcarVisto(mediaItemId: string, usuarioId: string | null): void {
    this.db.transaction(() => {
      this.guardarProgreso(mediaItemId, usuarioId, 0, 'visto')
      this.db.prepare('DELETE FROM proximas_a_ver WHERE media_item_id = ? AND usuario_id IS ?').run(mediaItemId, usuarioId)
    })()
  }

  /** No visto: se borra la fila (la ausencia de fila significa "no visto"). */
  marcarNoVisto(mediaItemId: string, usuarioId: string | null): void {
    this.db.prepare('DELETE FROM watch_progresses WHERE media_item_id = ? AND usuario_id IS ?').run(mediaItemId, usuarioId)
  }

  /** Estados de todos los ítems de una biblioteca: ausencia = no visto. */
  estadosDeBiblioteca(libraryId: string, usuarioId: string | null): Record<string, EstadoVisto> {
    const filas = this.db
      .prepare(
        `SELECT wp.media_item_id AS id, wp.estado FROM watch_progresses wp
           JOIN media_items m ON m.id = wp.media_item_id
          WHERE m.library_id = ? AND wp.usuario_id IS ? AND wp.estado != 'noVisto'`
      )
      .all(libraryId, usuarioId) as Fila[]
    return Object.fromEntries(filas.map((f) => [f.id as string, estadoVistoDesdeDb(f.estado)]))
  }

  /** Estado de cada episodio de una serie. */
  estadosDeSerie(seriesId: string, usuarioId: string | null): Record<string, { estado: EstadoVisto; posicionSegundos: number }> {
    const filas = this.db
      .prepare(
        `SELECT wp.media_item_id AS id, wp.estado, wp.posicion_segundos AS pos FROM watch_progresses wp
           JOIN media_items m ON m.id = wp.media_item_id
          WHERE m.series_id = ? AND wp.usuario_id IS ?`
      )
      .all(seriesId, usuarioId) as Fila[]
    return Object.fromEntries(filas.map((f) => [f.id as string, { estado: estadoVistoDesdeDb(f.estado), posicionSegundos: f.pos as number }]))
  }

  /** Marca como visto/no visto todos los episodios (de una temporada, o de la serie entera). */
  marcarSerie(seriesId: string, temporada: number | null, estado: 'visto' | 'noVisto', usuarioId: string | null): number {
    const ids = (
      this.db
        .prepare(
          `SELECT id FROM media_items WHERE series_id = ? AND estado_archivo = 'ok' ${temporada == null ? '' : 'AND numero_temporada = ?'}`
        )
        .all(...(temporada == null ? [seriesId] : [seriesId, temporada])) as Fila[]
    ).map((r) => r.id as string)
    this.db.transaction(() => {
      for (const id of ids) {
        if (estado === 'visto') this.marcarVisto(id, usuarioId)
        else this.marcarNoVisto(id, usuarioId)
      }
    })()
    return ids.length
  }

  // --- Próximas a ver

  estaEnProximas(mediaItemId: string, usuarioId: string | null): boolean {
    return !!this.db.prepare('SELECT 1 FROM proximas_a_ver WHERE media_item_id = ? AND usuario_id IS ?').get(mediaItemId, usuarioId)
  }

  agregarAProximas(mediaItemId: string, usuarioId: string | null): void {
    if (this.estaEnProximas(mediaItemId, usuarioId)) return
    this.db
      .prepare('INSERT INTO proximas_a_ver (media_item_id, usuario_id, agregado_en) VALUES (?, ?, ?)')
      .run(mediaItemId, usuarioId, ahoraDb())
  }

  quitarDeProximas(mediaItemId: string, usuarioId: string | null): void {
    this.db.prepare('DELETE FROM proximas_a_ver WHERE media_item_id = ? AND usuario_id IS ?').run(mediaItemId, usuarioId)
  }

  // --- colecciones

  private aColeccion(r: Fila): ColeccionDb {
    const posters = (
      this.db
        .prepare(
          `SELECT m.poster_url FROM collection_items ci JOIN media_items m ON m.id = ci.media_item_id
            WHERE ci.collection_id = ? AND m.estado_archivo = 'ok' AND m.poster_url IS NOT NULL
            ORDER BY ci.agregado_en DESC, ci.id DESC LIMIT 4`
        )
        .all(r.id) as Fila[]
    ).map((p) => p.poster_url as string)
    const conteo = (
      this.db
        .prepare(
          `SELECT COUNT(*) n FROM collection_items ci JOIN media_items m ON m.id = ci.media_item_id
            WHERE ci.collection_id = ? AND m.estado_archivo = 'ok'`
        )
        .get(r.id) as { n: number }
    ).n
    return { id: r.id, nombre: r.nombre, quitarAlVerse: boolDesdeDb(r.quitar_al_verse), creadoEn: r.creado_en, conteo, posters }
  }

  listarColecciones(usuarioId: string | null): ColeccionDb[] {
    return (
      this.db.prepare('SELECT * FROM collections WHERE usuario_id IS ? ORDER BY nombre COLLATE NOCASE').all(usuarioId) as Fila[]
    ).map((r) => this.aColeccion(r))
  }

  obtenerColeccion(id: string, usuarioId: string | null): ColeccionDb | null {
    const r = this.db.prepare('SELECT * FROM collections WHERE id = ? AND usuario_id IS ?').get(id, usuarioId) as Fila | undefined
    return r ? this.aColeccion(r) : null
  }

  crearColeccion(usuarioId: string | null, nombre: string, quitarAlVerse: boolean): string {
    const id = randomUUID()
    this.db
      .prepare('INSERT INTO collections (id, nombre, quitar_al_verse, usuario_id, creado_en) VALUES (?, ?, ?, ?, ?)')
      .run(id, nombre.trim(), boolADb(quitarAlVerse), usuarioId, ahoraDb())
    return id
  }

  renombrarColeccion(id: string, nombre: string): void {
    this.db.prepare('UPDATE collections SET nombre = ? WHERE id = ?').run(nombre.trim(), id)
  }

  setQuitarAlVerse(id: string, valor: boolean): void {
    this.db.prepare('UPDATE collections SET quitar_al_verse = ? WHERE id = ?').run(boolADb(valor), id)
  }

  /** Borra la colección (las películas no se tocan; las filas dependientes caen por ON DELETE CASCADE). */
  eliminarColeccion(id: string): void {
    this.db.prepare('DELETE FROM collections WHERE id = ?').run(id)
  }

  coleccionesDeItem(mediaItemId: string, usuarioId: string | null): ColeccionDb[] {
    return (
      this.db
        .prepare(
          `SELECT c.* FROM collections c JOIN collection_items ci ON ci.collection_id = c.id
            WHERE ci.media_item_id = ? AND c.usuario_id IS ? ORDER BY c.nombre COLLATE NOCASE`
        )
        .all(mediaItemId, usuarioId) as Fila[]
    ).map((r) => this.aColeccion(r))
  }

  /** IDs de los ítems visibles de una colección, del más reciente al más antiguo. */
  idsDeColeccion(id: string): string[] {
    return (
      this.db
        .prepare(
          `SELECT ci.media_item_id AS id FROM collection_items ci JOIN media_items m ON m.id = ci.media_item_id
            WHERE ci.collection_id = ? AND m.estado_archivo = 'ok' ORDER BY ci.agregado_en DESC, ci.id DESC`
        )
        .all(id) as Fila[]
    ).map((r) => r.id as string)
  }

  agregarItemAColeccion(collectionId: string, mediaItemId: string): void {
    this.db
      .prepare('INSERT OR IGNORE INTO collection_items (collection_id, media_item_id, agregado_en) VALUES (?, ?, ?)')
      .run(collectionId, mediaItemId, ahoraDb())
  }

  quitarItemDeColeccion(collectionId: string, mediaItemId: string): void {
    this.db.prepare('DELETE FROM collection_items WHERE collection_id = ? AND media_item_id = ?').run(collectionId, mediaItemId)
  }

  // --- progreso dentro de una colección (independiente del global)

  obtenerProgresoEnColeccion(collectionId: string, mediaItemId: string, usuarioId: string | null): ProgresoDb | null {
    const r = this.db
      .prepare('SELECT * FROM collection_watch_progresses WHERE collection_id = ? AND media_item_id = ? AND usuario_id IS ?')
      .get(collectionId, mediaItemId, usuarioId) as Fila | undefined
    return r ? aProgreso(r) : null
  }

  guardarProgresoEnColeccion(
    collectionId: string,
    mediaItemId: string,
    usuarioId: string | null,
    posicionSegundos: number,
    estado: EstadoVisto,
    pistaAudioId?: string | null,
    pistaSubtituloId?: string | null
  ): void {
    this.db.transaction(() => {
      const existente = this.db
        .prepare(
          'SELECT id, pista_audio_id, pista_subtitulo_id FROM collection_watch_progresses WHERE collection_id = ? AND media_item_id = ? AND usuario_id IS ?'
        )
        .get(collectionId, mediaItemId, usuarioId) as Fila | undefined
      const ahora = ahoraDb()
      if (!existente) {
        this.db
          .prepare(
            `INSERT INTO collection_watch_progresses (collection_id, media_item_id, usuario_id, posicion_segundos, estado, ultima_reproduccion, pista_audio_id, pista_subtitulo_id)
             VALUES (?, ?, ?, ?, ?, ?, ?, ?)`
          )
          .run(collectionId, mediaItemId, usuarioId, posicionSegundos, estado, ahora, pistaAudioId ?? null, pistaSubtituloId ?? null)
      } else {
        this.db
          .prepare(
            `UPDATE collection_watch_progresses SET posicion_segundos = ?, estado = ?, ultima_reproduccion = ?,
                    pista_audio_id = ?, pista_subtitulo_id = ? WHERE id = ?`
          )
          .run(posicionSegundos, estado, ahora, pistaAudioId ?? existente.pista_audio_id, pistaSubtituloId ?? existente.pista_subtitulo_id, existente.id)
      }
    })()
  }

  /** Visto dentro de la colección; con "quitar al verse" el ítem sale de ella. */
  marcarVistoEnColeccion(collectionId: string, mediaItemId: string, usuarioId: string | null): void {
    this.db.transaction(() => {
      this.guardarProgresoEnColeccion(collectionId, mediaItemId, usuarioId, 0, 'visto')
      const c = this.db.prepare('SELECT quitar_al_verse FROM collections WHERE id = ?').get(collectionId) as Fila | undefined
      if (c && boolDesdeDb(c.quitar_al_verse)) this.quitarItemDeColeccion(collectionId, mediaItemId)
    })()
  }

  marcarNoVistoEnColeccion(collectionId: string, mediaItemId: string, usuarioId: string | null): void {
    this.db
      .prepare('DELETE FROM collection_watch_progresses WHERE collection_id = ? AND media_item_id = ? AND usuario_id IS ?')
      .run(collectionId, mediaItemId, usuarioId)
  }

  /** "Continuar viendo" de una colección: ítems en progreso dentro de ella, con su posición. */
  continuarEnColeccion(collectionId: string, usuarioId: string | null): { id: string; posicionSegundos: number }[] {
    return (
      this.db
        .prepare(
          `SELECT cwp.media_item_id AS id, cwp.posicion_segundos AS pos FROM collection_watch_progresses cwp
             JOIN media_items m ON m.id = cwp.media_item_id AND m.estado_archivo = 'ok'
            WHERE cwp.collection_id = ? AND cwp.usuario_id IS ? AND cwp.estado = 'enProgreso'
            ORDER BY cwp.ultima_reproduccion DESC`
        )
        .all(collectionId, usuarioId) as Fila[]
    ).map((r) => ({ id: r.id as string, posicionSegundos: r.pos as number }))
  }

  // --- personas

  /** Nombre y foto de una persona tal como figuran en el reparto o equipo del catálogo (el primero que aparezca). */
  datosLocalesPersona(tmdbPersonId: number): { nombre: string; fotoUrl: string | null; enEquipo: boolean } | null {
    const r = this.db
      .prepare(
        `SELECT json_extract(j.value, '$.nombre') AS nombre, json_extract(j.value, '$.fotoUrl') AS foto, j.es_equipo AS equipo FROM (
           SELECT je.value, 0 AS es_equipo FROM media_items m, json_each(CASE WHEN json_valid(m.reparto) THEN m.reparto ELSE '[]' END) je
           UNION ALL SELECT je.value, 0 FROM series s, json_each(CASE WHEN json_valid(s.reparto) THEN s.reparto ELSE '[]' END) je
           UNION ALL SELECT je.value, 1 FROM media_items m, json_each(CASE WHEN json_valid(m.equipo) THEN m.equipo ELSE '[]' END) je
           UNION ALL SELECT je.value, 1 FROM series s, json_each(CASE WHEN json_valid(s.equipo) THEN s.equipo ELSE '[]' END) je
         ) j WHERE json_extract(j.value, '$.id') = ? ORDER BY json_extract(j.value, '$.fotoUrl') IS NULL, j.es_equipo LIMIT 1`
      )
      .get(tmdbPersonId) as Fila | undefined
    return r && r.nombre ? { nombre: r.nombre, fotoUrl: r.foto ?? null, enEquipo: r.equipo === 1 } : null
  }

  /** Progreso de cada ítem dentro de una colección (para mostrar en sus tarjetas el avance propio de la colección). */
  progresosDeColeccion(collectionId: string, usuarioId: string | null): Map<string, ProgresoDb> {
    const filas = this.db
      .prepare('SELECT * FROM collection_watch_progresses WHERE collection_id = ? AND usuario_id IS ?')
      .all(collectionId, usuarioId) as Fila[]
    return new Map(filas.map((f) => [f.media_item_id as string, aProgreso(f)]))
  }

  /** Películas y series del catálogo donde aparece una persona de TMDb (reparto o equipo). */
  enCatalogoPersona(tmdbPersonId: number): { kind: 'pelicula' | 'serie'; id: string; rol: string | null }[] {
    const filas = this.db
      .prepare(
        `SELECT 'pelicula' AS kind, m.id, m.titulo, (
                  SELECT COALESCE(json_extract(j.value, '$.rol'), json_extract(j.value, '$.personaje'))
                    FROM json_each(CASE WHEN json_valid(m.reparto) THEN m.reparto ELSE '[]' END) j
                   WHERE json_extract(j.value, '$.id') = @p LIMIT 1) AS rol_reparto,
                (SELECT 1 FROM json_each(CASE WHEN json_valid(m.equipo) THEN m.equipo ELSE '[]' END) k
                   WHERE json_extract(k.value, '$.id') = @p LIMIT 1) AS en_equipo
           FROM media_items m
          WHERE m.series_id IS NULL AND m.estado_archivo = 'ok'
            AND (EXISTS (SELECT 1 FROM json_each(CASE WHEN json_valid(m.reparto) THEN m.reparto ELSE '[]' END) j WHERE json_extract(j.value, '$.id') = @p)
              OR EXISTS (SELECT 1 FROM json_each(CASE WHEN json_valid(m.equipo) THEN m.equipo ELSE '[]' END) k WHERE json_extract(k.value, '$.id') = @p))
         UNION ALL
         SELECT 'serie', s.id, s.titulo, (
                  SELECT COALESCE(json_extract(j.value, '$.rol'), json_extract(j.value, '$.personaje'))
                    FROM json_each(CASE WHEN json_valid(s.reparto) THEN s.reparto ELSE '[]' END) j
                   WHERE json_extract(j.value, '$.id') = @p LIMIT 1),
                (SELECT 1 FROM json_each(CASE WHEN json_valid(s.equipo) THEN s.equipo ELSE '[]' END) k
                   WHERE json_extract(k.value, '$.id') = @p LIMIT 1)
           FROM series s
          WHERE s.estado_archivo = 'ok'
            AND (EXISTS (SELECT 1 FROM json_each(CASE WHEN json_valid(s.reparto) THEN s.reparto ELSE '[]' END) j WHERE json_extract(j.value, '$.id') = @p)
              OR EXISTS (SELECT 1 FROM json_each(CASE WHEN json_valid(s.equipo) THEN s.equipo ELSE '[]' END) k WHERE json_extract(k.value, '$.id') = @p))
          ORDER BY 3 COLLATE NOCASE`
      )
      .all({ p: tmdbPersonId }) as Fila[]
    return filas.map((f) => ({ kind: f.kind, id: f.id, rol: f.rol_reparto ?? null }))
  }
}
