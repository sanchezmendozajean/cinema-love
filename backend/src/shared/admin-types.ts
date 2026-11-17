import type { TipoBiblioteca } from './enums'
import type { Persona } from './api-types'

/** Tipos de la API de administración (`/api/v1/admin/*`, solo listener interno). */

export interface BibliotecaAdmin {
  id: string
  nombre: string
  tipo: TipoBiblioteca
  ruta: string
  disponible: boolean
  items: number
  series: number
  huerfanos: number
}

export interface Huerfano {
  tipo: 'item' | 'serie'
  id: string
  rutaRelativa: string
  titulo: string | null
  serie: string | null
}

export type EstadoJob = 'corriendo' | 'completado' | 'error' | 'cancelado'

export interface JobResumen {
  id: string
  tipo: 'escaneo' | 'metadata'
  bibliotecaId: string | null
  titulo: string
  estado: EstadoJob
  progreso: { hechos: number; total: number } | null
  resultado: unknown
  error: string | null
  creadoEn: number
  terminadoEn: number | null
}

export interface JobDetalle extends JobResumen {
  lineas: string[]
}

export interface ResultadoEscaneoJob {
  nuevos: number
  reconciliados: number
  noEncontrados: number
  extrasNuevos: number
  seriesNuevas: number
  seriesNoEncontradas: number
  reconciliadosPorHash: { id: string; titulo: string | null; rutaRelativa: string }[]
}

export interface PendienteMetadata {
  id: string
  ruta: string
  titulo: string
  anio?: number
  tipo?: 'item' | 'serie'
}

export interface ResultadoMetadataJob {
  total: number
  resueltos: number
  ambiguos: PendienteMetadata[]
  sinCoincidencia: PendienteMetadata[]
  cancelado: boolean
}

export interface ResultadoTmdb {
  id: number
  titulo: string
  tituloOriginal: string | null
  anio: number | null
  posterUrl: string | null
  sinopsis: string | null
}

export interface EstadoTmdbClave {
  configurada: boolean
  mascara: string | null
}

/** Campos editables a mano; solo se envían los que cambian. */
export interface EdicionMetadata {
  titulo?: string | null
  tituloOriginal?: string | null
  anio?: number | null
  sinopsis?: string | null
  estudio?: string | null
  idiomaOriginal?: string | null
  duracionSegundos?: number | null
  posterUrl?: string | null
  backdropUrl?: string | null
  generos?: string[]
  reparto?: Persona[]
  equipo?: Persona[]
}

/** Resultado de homologar el nombre de una película: nombre actual → nuevo (relativos a la biblioteca). */
export interface PlanHomologar {
  actual: string
  nuevo: string
  acompanantes: number
  extras: number
}

// ------------------------------------------------------------------ servidor, usuarios y seguridad

export interface ConfigServidor {
  protocolo: 'http' | 'https'
  puertoHttp: number
  puertoHttps: number
  nombre: string
  mdns: boolean
  autostart: boolean
}

export interface EstadoServidor {
  activo: boolean
  protocolo: 'http' | 'https'
  puerto: number
  nombre: string
  direcciones: string[]
  urls: string[]
  huella: string | null
  anunciando: boolean
}

export interface VistaServidor {
  config: ConfigServidor
  estado: EstadoServidor
}

export interface UsuarioAdmin {
  id: string
  nombreUsuario: string
  rol: string
  creadoEn: string
  sesiones: number
}

export interface SesionConversion {
  id: string
  modo: 'remux' | 'transcode'
  motivo: string
  inicio: number
  titulo: string
  quien: string
  cliente: string
}

export interface SesionDirecta {
  quien: string
  titulo: string
  cliente: string
  desde: number
  ultimaVez: number
}

export interface SesionesActivas {
  conversiones: SesionConversion[]
  directas: SesionDirecta[]
}

export interface InfoArchivo {
  bibliotecaId: string
  bibliotecaNombre: string
  /** Carpeta raíz de la biblioteca. */
  carpetaBiblioteca: string
  /** Ruta del archivo relativa a la biblioteca, con `/`. */
  rutaRelativa: string
  /** El archivo está en el disco ahora mismo. */
  existe: boolean
  bytes: number | null
}
