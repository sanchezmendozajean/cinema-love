import type { EstadoArchivo, EstadoVisto, TipoBiblioteca } from '../../shared/enums'
import type { Persona } from '../db/codecs'

export interface Biblioteca {
  id: string
  nombre: string
  tipo: TipoBiblioteca
  rutaActual: string
  creadoEn: Date
}

export interface MetadataComun {
  tmdbId: string | null
  titulo: string | null
  tituloOriginal: string | null
  idiomaOriginal: string | null
  anio: number | null
  sinopsis: string | null
  estudio: string | null
  posterUrl: string | null
  backdropUrl: string | null
  generos: string[]
  reparto: Persona[]
  equipo: Persona[]
}

/** Película (series_id nulo) o episodio. */
export interface ItemMedia extends MetadataComun {
  id: string
  libraryId: string
  rutaRelativa: string
  hashRespaldo: string | null
  estadoArchivo: EstadoArchivo
  seriesId: string | null
  numeroTemporada: number | null
  numeroEpisodio: number | null
  duracionSegundos: number | null
  creadoEn: Date
  actualizadoEn: Date
}

export interface Serie extends MetadataComun {
  id: string
  libraryId: string
  rutaRelativa: string
  estadoArchivo: EstadoArchivo
  creadoEn: Date
  actualizadoEn: Date
}

export interface Temporada {
  id: string
  seriesId: string
  numeroTemporada: number
  tmdbId: string | null
  titulo: string | null
  sinopsis: string | null
  posterUrl: string | null
  creadoEn: Date
  actualizadoEn: Date
}

export interface Extra {
  id: string
  mediaItemId: string
  rutaRelativa: string
  hashRespaldo: string | null
  estadoArchivo: EstadoArchivo
  titulo: string | null
  creadoEn: Date
}

export interface Progreso {
  id: number
  mediaItemId: string
  usuarioId: string | null
  posicionSegundos: number
  estado: EstadoVisto
  ultimaReproduccion: Date | null
  pistaAudioId: string | null
  pistaSubtituloId: string | null
}

export interface ProximaAVer {
  id: number
  mediaItemId: string
  usuarioId: string | null
  agregadoEn: Date
}

export interface Coleccion {
  id: string
  nombre: string
  quitarAlVerse: boolean
  usuarioId: string | null
  creadoEn: Date
  cantidadItems: number
}

export interface ItemDeColeccion {
  id: number
  collectionId: string
  mediaItemId: string
  agregadoEn: Date
}
