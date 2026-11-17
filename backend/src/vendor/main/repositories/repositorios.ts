import type Database from 'better-sqlite3'
import {
  boolDesdeDb,
  estadoArchivoDesdeDb,
  estadoVistoDesdeDb,
  fechaDesdeDb,
  fechaNullDesdeDb,
  generosDesdeDb,
  personasDesdeDb,
  tipoBibliotecaDesdeDb
} from '../db/codecs'
import type {
  Biblioteca,
  Coleccion,
  Extra,
  ItemDeColeccion,
  ItemMedia,
  MetadataComun,
  Progreso,
  ProximaAVer,
  Serie,
  Temporada
} from './tipos'

// Las filas vienen de SQLite sin tipar; los mapeadores son el único lugar que las interpreta.
// eslint-disable-next-line @typescript-eslint/no-explicit-any
type Fila = Record<string, any>

const metadata = (r: Fila): MetadataComun => ({
  tmdbId: r.tmdb_id,
  titulo: r.titulo,
  tituloOriginal: r.titulo_original,
  idiomaOriginal: r.idioma_original,
  anio: r.anio,
  sinopsis: r.sinopsis,
  estudio: r.estudio,
  posterUrl: r.poster_url,
  backdropUrl: r.backdrop_url,
  generos: generosDesdeDb(r.generos),
  reparto: personasDesdeDb(r.reparto),
  equipo: personasDesdeDb(r.equipo)
})

const aBiblioteca = (r: Fila): Biblioteca => ({
  id: r.id,
  nombre: r.nombre,
  tipo: tipoBibliotecaDesdeDb(r.tipo),
  rutaActual: r.ruta_actual,
  creadoEn: fechaDesdeDb(r.creado_en)
})

const aItem = (r: Fila): ItemMedia => ({
  ...metadata(r),
  id: r.id,
  libraryId: r.library_id,
  rutaRelativa: r.ruta_relativa,
  hashRespaldo: r.hash_respaldo,
  estadoArchivo: estadoArchivoDesdeDb(r.estado_archivo),
  seriesId: r.series_id,
  numeroTemporada: r.numero_temporada,
  numeroEpisodio: r.numero_episodio,
  duracionSegundos: r.duracion_segundos,
  creadoEn: fechaDesdeDb(r.creado_en),
  actualizadoEn: fechaDesdeDb(r.actualizado_en)
})

const aSerie = (r: Fila): Serie => ({
  ...metadata(r),
  id: r.id,
  libraryId: r.library_id,
  rutaRelativa: r.ruta_relativa,
  estadoArchivo: estadoArchivoDesdeDb(r.estado_archivo),
  creadoEn: fechaDesdeDb(r.creado_en),
  actualizadoEn: fechaDesdeDb(r.actualizado_en)
})

const aTemporada = (r: Fila): Temporada => ({
  id: r.id,
  seriesId: r.series_id,
  numeroTemporada: r.numero_temporada,
  tmdbId: r.tmdb_id,
  titulo: r.titulo,
  sinopsis: r.sinopsis,
  posterUrl: r.poster_url,
  creadoEn: fechaDesdeDb(r.creado_en),
  actualizadoEn: fechaDesdeDb(r.actualizado_en)
})

const aExtra = (r: Fila): Extra => ({
  id: r.id,
  mediaItemId: r.media_item_id,
  rutaRelativa: r.ruta_relativa,
  hashRespaldo: r.hash_respaldo,
  estadoArchivo: estadoArchivoDesdeDb(r.estado_archivo),
  titulo: r.titulo,
  creadoEn: fechaDesdeDb(r.creado_en)
})

const aProgreso = (r: Fila): Progreso => ({
  id: r.id,
  mediaItemId: r.media_item_id,
  usuarioId: r.usuario_id,
  posicionSegundos: r.posicion_segundos,
  estado: estadoVistoDesdeDb(r.estado),
  ultimaReproduccion: fechaNullDesdeDb(r.ultima_reproduccion),
  pistaAudioId: r.pista_audio_id,
  pistaSubtituloId: r.pista_subtitulo_id
})

const aProxima = (r: Fila): ProximaAVer => ({
  id: r.id,
  mediaItemId: r.media_item_id,
  usuarioId: r.usuario_id,
  agregadoEn: fechaDesdeDb(r.agregado_en)
})

const aColeccion = (r: Fila): Coleccion => ({
  id: r.id,
  nombre: r.nombre,
  quitarAlVerse: boolDesdeDb(r.quitar_al_verse),
  usuarioId: r.usuario_id,
  creadoEn: fechaDesdeDb(r.creado_en),
  cantidadItems: r.cantidad
})

const aItemDeColeccion = (r: Fila): ItemDeColeccion => ({
  id: r.id,
  collectionId: r.collection_id,
  mediaItemId: r.media_item_id,
  agregadoEn: fechaDesdeDb(r.agregado_en)
})

/** Repositorios de lectura. `usuarioId` null = admin local (`usuario_id IS NULL`). */
export class Repositorios {
  constructor(private readonly db: Database.Database) {}

  private todos<T>(sql: string, mapa: (r: Fila) => T, ...params: unknown[]): T[] {
    return (this.db.prepare(sql).all(...params) as Fila[]).map(mapa)
  }
  private uno<T>(sql: string, mapa: (r: Fila) => T, ...params: unknown[]): T | null {
    const r = this.db.prepare(sql).get(...params) as Fila | undefined
    return r ? mapa(r) : null
  }

  // --- bibliotecas
  listarBibliotecas = (): Biblioteca[] =>
    this.todos('SELECT * FROM libraries ORDER BY nombre COLLATE NOCASE', aBiblioteca)
  obtenerBiblioteca = (id: string): Biblioteca | null =>
    this.uno('SELECT * FROM libraries WHERE id = ?', aBiblioteca, id)

  // --- ítems (películas y episodios)
  obtenerItem = (id: string): ItemMedia | null =>
    this.uno('SELECT * FROM media_items WHERE id = ?', aItem, id)
  listarPeliculas = (libraryId?: string): ItemMedia[] =>
    libraryId
      ? this.todos(
          'SELECT * FROM media_items WHERE series_id IS NULL AND library_id = ? ORDER BY titulo COLLATE NOCASE',
          aItem,
          libraryId
        )
      : this.todos(
          'SELECT * FROM media_items WHERE series_id IS NULL ORDER BY titulo COLLATE NOCASE',
          aItem
        )
  listarEpisodios = (seriesId: string, temporada?: number): ItemMedia[] =>
    temporada == null
      ? this.todos(
          'SELECT * FROM media_items WHERE series_id = ? ORDER BY numero_temporada, numero_episodio',
          aItem,
          seriesId
        )
      : this.todos(
          'SELECT * FROM media_items WHERE series_id = ? AND numero_temporada = ? ORDER BY numero_episodio',
          aItem,
          seriesId,
          temporada
        )
  contarItems = (): { peliculas: number; episodios: number } => {
    const r = this.db
      .prepare(
        'SELECT SUM(series_id IS NULL) AS p, SUM(series_id IS NOT NULL) AS e FROM media_items'
      )
      .get() as { p: number | null; e: number | null }
    return { peliculas: r.p ?? 0, episodios: r.e ?? 0 }
  }

  // --- series y temporadas
  listarSeries = (libraryId?: string): Serie[] =>
    libraryId
      ? this.todos(
          'SELECT * FROM series WHERE library_id = ? ORDER BY titulo COLLATE NOCASE',
          aSerie,
          libraryId
        )
      : this.todos('SELECT * FROM series ORDER BY titulo COLLATE NOCASE', aSerie)
  obtenerSerie = (id: string): Serie | null =>
    this.uno('SELECT * FROM series WHERE id = ?', aSerie, id)
  listarTemporadas = (seriesId: string): Temporada[] =>
    this.todos(
      'SELECT * FROM seasons WHERE series_id = ? ORDER BY numero_temporada',
      aTemporada,
      seriesId
    )

  // --- listados que expone la API (solo archivos presentes, orden de Flutter)
  peliculasVisibles = (libraryId: string): ItemMedia[] =>
    this.todos(
      "SELECT * FROM media_items WHERE library_id = ? AND series_id IS NULL AND estado_archivo = 'ok' ORDER BY ruta_relativa",
      aItem,
      libraryId
    )
  seriesVisibles = (libraryId: string): Serie[] =>
    this.todos(
      "SELECT * FROM series WHERE library_id = ? AND estado_archivo = 'ok' ORDER BY ruta_relativa",
      aSerie,
      libraryId
    )
  episodiosVisibles = (seriesId: string, temporada?: number): ItemMedia[] =>
    this.todos(
      `SELECT * FROM media_items WHERE series_id = ? AND estado_archivo = 'ok'
          ${temporada == null ? '' : 'AND numero_temporada = ?'} ORDER BY numero_temporada, numero_episodio`,
      aItem,
      ...(temporada == null ? [seriesId] : [seriesId, temporada])
    )
  obtenerExtra = (id: string): Extra | null => this.uno('SELECT * FROM extras WHERE id = ?', aExtra, id)

  // --- extras
  listarExtras = (mediaItemId: string): Extra[] =>
    this.todos(
      'SELECT * FROM extras WHERE media_item_id = ? ORDER BY titulo COLLATE NOCASE',
      aExtra,
      mediaItemId
    )

  // --- progreso
  obtenerProgreso = (mediaItemId: string, usuarioId: string | null): Progreso | null =>
    this.uno(
      'SELECT * FROM watch_progresses WHERE media_item_id = ? AND usuario_id IS ?',
      aProgreso,
      mediaItemId,
      usuarioId
    )
  listarProgresos = (usuarioId: string | null): Progreso[] =>
    this.todos(
      'SELECT * FROM watch_progresses WHERE usuario_id IS ? ORDER BY ultima_reproduccion DESC',
      aProgreso,
      usuarioId
    )
  listarEnProgreso = (usuarioId: string | null): Progreso[] =>
    this.todos(
      "SELECT * FROM watch_progresses WHERE usuario_id IS ? AND estado = 'enProgreso' ORDER BY ultima_reproduccion DESC",
      aProgreso,
      usuarioId
    )

  // --- próximas a ver
  listarProximasAVer = (usuarioId: string | null): ProximaAVer[] =>
    this.todos(
      'SELECT * FROM proximas_a_ver WHERE usuario_id IS ? ORDER BY agregado_en DESC',
      aProxima,
      usuarioId
    )

  // --- colecciones
  listarColecciones = (usuarioId: string | null): Coleccion[] =>
    this.todos(
      `SELECT c.*, (SELECT COUNT(*) FROM collection_items i WHERE i.collection_id = c.id) AS cantidad
         FROM collections c WHERE c.usuario_id IS ? ORDER BY c.nombre COLLATE NOCASE`,
      aColeccion,
      usuarioId
    )
  listarItemsDeColeccion = (collectionId: string): ItemDeColeccion[] =>
    this.todos(
      'SELECT * FROM collection_items WHERE collection_id = ? ORDER BY agregado_en, id',
      aItemDeColeccion,
      collectionId
    )
}
