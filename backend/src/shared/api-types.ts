/** DTOs de la API HTTP v1 (specs/04-api-servidor.md §4). Compatibles con los clientes Flutter. */
import type { EstadoVisto, TipoBiblioteca } from './enums'

export interface Persona {
  id?: number
  nombre: string
  rol?: string
  fotoUrl?: string
}

export interface Library {
  id: string
  nombre: string
  tipo: TipoBiblioteca
  creadoEn: string
  /** Nuevos (aditivos): títulos visibles, cuántos sin metadatos y hasta 3 backdrops para el collage. */
  conteo?: number
  sinMetadatos?: number
  backdrops?: string[]
}

export interface MediaItem {
  id: string
  libraryId: string
  seriesId: string | null
  numeroTemporada: number | null
  numeroEpisodio: number | null
  tmdbId: string | null
  titulo: string | null
  anio: number | null
  posterUrl: string | null
  backdropUrl: string | null
  duracionSegundos: number | null
  creadoEn: string
  // detalle
  estadoArchivo?: 'ok' | 'noEncontrado'
  tituloArchivo?: string
  tituloOriginal?: string | null
  idiomaOriginal?: string | null
  sinopsis?: string | null
  estudio?: string | null
  generos?: string[]
  reparto?: Persona[]
  equipo?: Persona[]
}

export interface Serie {
  id: string
  libraryId: string
  tmdbId: string | null
  titulo: string | null
  anio: number | null
  posterUrl: string | null
  backdropUrl: string | null
  creadoEn: string
  tituloArchivo?: string
  tituloOriginal?: string | null
  idiomaOriginal?: string | null
  sinopsis?: string | null
  estudio?: string | null
  generos?: string[]
  reparto?: Persona[]
  equipo?: Persona[]
}

export interface Season {
  id: string
  seriesId: string
  numeroTemporada: number
  titulo: string | null
  sinopsis: string | null
  posterUrl: string | null
}

export interface Extra {
  id: string
  mediaItemId: string
  titulo: string | null
}

export interface Progreso {
  posicionSegundos: number
  estado: EstadoVisto
  pistaAudioId?: string
  pistaSubtituloId?: string
}

export interface Coleccion {
  id: string
  nombre: string
  quitarAlVerse: boolean
  creadoEn: string
  /** Nuevos: cantidad de ítems y hasta 4 pósters para el mosaico. */
  conteo?: number
  posters?: string[]
}

// --- sistema

export interface Health {
  status: 'ok'
  app: 'cinemalove'
  serverId: string
  nombre: string
  apiVersion: 1
  plataforma: 'electron'
}

export type Capability = 'browse' | 'play' | 'progress' | 'userLists' | 'admin'
export type TipoPrincipal = 'admin' | 'token' | 'usuario'

export interface Me {
  principal: TipoPrincipal
  nombreUsuario?: string
  capabilities: Capability[]
}

export interface LoginRespuesta {
  token: string
  usuarioId: string
  nombreUsuario: string
}

// --- catálogo unificado

export interface ProgresoResumen {
  estado: EstadoVisto
  posicionSegundos: number
}

export interface ElementoPelicula {
  kind: 'pelicula'
  /** Sin metadatos de TMDb: `titulo` es null y `tituloArchivo` trae el nombre derivado del archivo. */
  sinMetadatos?: true
  tituloArchivo?: string
  id: string
  libraryId: string
  titulo: string | null
  anio: number | null
  posterUrl: string | null
  generos: string[]
  duracionSegundos: number | null
  progreso: ProgresoResumen | null
}

export interface ElementoSerie {
  kind: 'serie'
  sinMetadatos?: true
  tituloArchivo?: string
  id: string
  libraryId: string
  titulo: string | null
  anio: number | null
  posterUrl: string | null
  generos: string[]
  temporadas: number
  episodios: number
  episodiosVistos: number
}

export type ElementoCatalogo = ElementoPelicula | ElementoSerie

export type OrdenCatalogo = 'titulo' | '-titulo' | 'anio' | '-anio' | 'agregado' | '-agregado' | 'reproducido' | '-reproducido'

export interface FiltrosCatalogo {
  q?: string
  generos?: string[]
  bibliotecas?: string[]
  tipo?: 'pelicula' | 'serie'
  anioDesde?: number
  anioHasta?: number
  estado?: EstadoVisto
  orden?: OrdenCatalogo
  pagina?: number
  tamano?: number
}

export interface Facetas {
  generos: { valor: string; conteo: number }[]
  bibliotecas: { id: string; nombre: string; tipo: TipoBiblioteca; conteo: number }[]
  anios: { min: number | null; max: number | null }
}

export interface RespuestaCatalogo {
  total: number
  pagina: number
  tamano: number
  elementos: ElementoCatalogo[]
  facetas: Facetas
}

// --- inicio

export interface ElementoContinuar {
  kind: 'pelicula' | 'episodio'
  id: string
  titulo: string | null
  posterUrl: string | null
  backdropUrl: string | null
  duracionSegundos: number | null
  progreso: ProgresoResumen
  /** Solo episodios. */
  serie?: { id: string; titulo: string | null; posterUrl: string | null }
  /** Solo episodios, formato `S01E02`. */
  episodio?: string
}

export interface Destacado {
  kind: 'pelicula' | 'episodio' | 'serie'
  id: string
  titulo: string | null
  anio: number | null
  sinopsis: string | null
  generos: string[]
  posterUrl: string | null
  backdropUrl: string | null
  duracionSegundos: number | null
  progreso: ProgresoResumen | null
  serie?: { id: string; titulo: string | null }
  episodio?: string
}

export interface Home {
  continuarViendo: ElementoContinuar[]
  proximasAVer: ElementoCatalogo[]
  agregadosRecientemente: ElementoCatalogo[]
  vistosRecientemente: ElementoCatalogo[]
  /** Hasta 5 títulos para el héroe rotativo de Inicio. */
  destacados: Destacado[]
  /** Primer destacado (compatibilidad). */
  destacado: Destacado | null
}

// --- ajustes → datos

export interface TamanoCarpeta {
  archivos: number
  bytes: number
}

export interface InfoDatos {
  carpeta: string
  /** Imágenes que CinemaLove descargó (`images/cl/`); las de Flutter no se cuentan ni se tocan. */
  imagenes: TamanoCarpeta
  /** Subtítulos convertidos y otros temporales de reproducción (`transcode/`). */
  temporales: TamanoCarpeta
  /** Tamaño de la base de datos (`cinemalove.sqlite`). */
  baseBytes: number
}
