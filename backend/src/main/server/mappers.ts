import type { Extra, Library, MediaItem, Persona, Season, Serie } from '../../shared/api-types'
import type { Persona as PersonaDb } from '../db/codecs'
import { nombreDeArchivo } from '../db/texto'
import type * as T from '../repositories/tipos'

const iso = (d: Date): string => d.toISOString()

/** Quita los nulos de una persona (Flutter omite las claves ausentes). */
const persona = (p: PersonaDb): Persona => ({
  ...(p.id != null ? { id: p.id } : {}),
  nombre: p.nombre,
  ...(p.rol ? { rol: p.rol } : {}),
  ...(p.fotoUrl ? { fotoUrl: p.fotoUrl } : {})
})

// Nunca se exponen ruta_actual, ruta_relativa ni hash_respaldo (specs/04 §1).

export const libraryToJson = (l: T.Biblioteca): Library => ({
  id: l.id,
  nombre: l.nombre,
  tipo: l.tipo,
  creadoEn: iso(l.creadoEn)
})

export function mediaItemToJson(i: T.ItemMedia, detalle = false): MediaItem {
  const base: MediaItem = {
    id: i.id,
    libraryId: i.libraryId,
    seriesId: i.seriesId,
    numeroTemporada: i.numeroTemporada,
    numeroEpisodio: i.numeroEpisodio,
    tmdbId: i.tmdbId,
    titulo: i.titulo,
    anio: i.anio,
    posterUrl: i.posterUrl,
    backdropUrl: i.backdropUrl,
    duracionSegundos: i.duracionSegundos,
    creadoEn: iso(i.creadoEn)
  }
  if (!detalle) return { ...base, generos: i.generos }
  return {
    ...base,
    estadoArchivo: i.estadoArchivo,
    // Sin metadatos de TMDb: nombre derivado del archivo (la ruta real no se expone).
    ...(i.titulo == null ? { tituloArchivo: nombreDeArchivo(i.rutaRelativa) } : {}),
    tituloOriginal: i.tituloOriginal,
    idiomaOriginal: i.idiomaOriginal,
    sinopsis: i.sinopsis,
    estudio: i.estudio,
    generos: i.generos,
    reparto: i.reparto.map(persona),
    equipo: i.equipo.map(persona)
  }
}

export function serieToJson(s: T.Serie, detalle = false): Serie {
  const base: Serie = {
    id: s.id,
    libraryId: s.libraryId,
    tmdbId: s.tmdbId,
    titulo: s.titulo,
    anio: s.anio,
    posterUrl: s.posterUrl,
    backdropUrl: s.backdropUrl,
    creadoEn: iso(s.creadoEn)
  }
  if (!detalle) return { ...base, generos: s.generos }
  return {
    ...base,
    ...(s.titulo == null ? { tituloArchivo: nombreDeArchivo(s.rutaRelativa) } : {}),
    tituloOriginal: s.tituloOriginal,
    idiomaOriginal: s.idiomaOriginal,
    sinopsis: s.sinopsis,
    estudio: s.estudio,
    generos: s.generos,
    reparto: s.reparto.map(persona),
    equipo: s.equipo.map(persona)
  }
}

export const seasonToJson = (s: T.Temporada): Season => ({
  id: s.id,
  seriesId: s.seriesId,
  numeroTemporada: s.numeroTemporada,
  titulo: s.titulo,
  sinopsis: s.sinopsis,
  posterUrl: s.posterUrl
})

export const extraToJson = (e: T.Extra): Extra => ({ id: e.id, mediaItemId: e.mediaItemId, titulo: e.titulo })
