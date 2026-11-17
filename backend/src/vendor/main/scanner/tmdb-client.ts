import type { TipoBiblioteca } from '../../shared/enums'
import type { Persona } from '../db/codecs'
import type { ResultadoBusqueda } from './tmdb-match'

/** Error de TMDb con un mensaje listo para mostrar. `fatal`: no tiene sentido seguir (clave inválida, sin red). */
export class TmdbError extends Error {
  constructor(
    mensaje: string,
    readonly fatal = true,
    readonly status?: number
  ) {
    super(mensaje)
  }
}

export interface DetalleTmdb {
  id: number
  titulo: string
  tituloOriginal: string | null
  idiomaOriginal: string | null
  anio: number | null
  sinopsis: string | null
  estudio: string | null
  posterUrl: string | null
  backdropUrl: string | null
  duracionSegundos: number | null
  generos: string[]
  reparto: Persona[]
  equipo: Persona[]
}

export interface EpisodioTmdb {
  tmdbId: number | null
  numeroEpisodio: number
  nombre: string | null
  sinopsis: string | null
  stillUrl: string | null
  duracionSegundos: number | null
}

export interface TemporadaTmdb {
  tmdbId: number | null
  nombre: string | null
  sinopsis: string | null
  posterUrl: string | null
  episodios: EpisodioTmdb[]
  reparto: Persona[]
  equipo: Persona[]
}

const BASE = 'https://api.themoviedb.org/3'
const POSTER = 'https://image.tmdb.org/t/p/w500'
const BACKDROP = 'https://image.tmdb.org/t/p/w1280'
const PERFIL = 'https://image.tmdb.org/t/p/w185'
const STILL = 'https://image.tmdb.org/t/p/w300'

// es-MX y no es-ES: TMDb guarda traducciones por país y los títulos de España y Latinoamérica suelen diferir. Lo que
// no tenga traducción en es-MX se completa CAMPO POR CAMPO con en-US (no se pierde el resto de la respuesta).
const IDIOMA_PRINCIPAL = 'es-MX'
const IDIOMA_FALLBACK = 'en-US'

/** Cargos del equipo técnico que valen la pena en una ficha curada (el resto del crew de TMDb es demasiado extenso). */
const ROLES_EQUIPO: Record<string, string> = {
  Director: 'Director',
  Screenplay: 'Guionista',
  Writer: 'Guionista',
  Story: 'Guionista',
  Producer: 'Productor',
  'Executive Producer': 'Productor ejecutivo'
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type Json = Record<string, any>

const anioDeFecha = (f: unknown): number | null => {
  if (typeof f !== 'string' || f.length < 4) return null
  const n = Number.parseInt(f.slice(0, 4), 10)
  return Number.isNaN(n) ? null : n
}
const url = (base: string, path: unknown): string | null => (typeof path === 'string' && path ? `${base}${path}` : null)
const texto = (v: unknown): string | null => (typeof v === 'string' ? v : null)

export interface OpcionesTmdb {
  fetch?: typeof fetch
  /** Peticiones simultáneas (TMDb pide moderación). */
  concurrencia?: number
  /** Reintentos ante 429 (con espera creciente o `Retry-After`). */
  reintentos429?: number
  /** Espera base entre reintentos (ms); inyectable para pruebas. */
  esperaBaseMs?: number
  timeoutMs?: number
}

/**
 * Cliente de la API v3 de TMDb con la API key del usuario. Todas las peticiones pasan por una cola con concurrencia
 * limitada (por defecto 4) y reintentan con espera creciente si TMDb responde 429 (specs/06 §5).
 */
export class TmdbClient {
  private readonly pedir: typeof fetch
  private readonly limite: number
  private readonly reintentos: number
  private readonly esperaBase: number
  private readonly timeout: number
  private activas = 0
  private readonly cola: (() => void)[] = []

  constructor(o: OpcionesTmdb = {}) {
    this.pedir = o.fetch ?? fetch
    this.limite = o.concurrencia ?? 4
    this.reintentos = o.reintentos429 ?? 4
    this.esperaBase = o.esperaBaseMs ?? 1000
    this.timeout = o.timeoutMs ?? 15_000
  }

  private async turno<T>(tarea: () => Promise<T>): Promise<T> {
    if (this.activas >= this.limite) await new Promise<void>((r) => this.cola.push(r))
    this.activas++
    try {
      return await tarea()
    } finally {
      this.activas--
      this.cola.shift()?.()
    }
  }

  private async get(ruta: string, params: Record<string, string>): Promise<Json> {
    const u = `${BASE}/${ruta}?${new URLSearchParams(params).toString()}`
    return this.turno(async () => {
      for (let intento = 0; ; intento++) {
        let r: Response
        try {
          r = await this.pedir(u, { signal: AbortSignal.timeout(this.timeout) })
        } catch {
          throw new TmdbError('No se pudo conectar con TMDb. Revisa tu conexión a internet.')
        }
        if (r.status === 429 && intento < this.reintentos) {
          const pedido = Number(r.headers.get('retry-after'))
          const espera = Number.isFinite(pedido) && pedido > 0 ? pedido * 1000 : this.esperaBase * 2 ** intento
          await new Promise((res) => setTimeout(res, espera))
          continue
        }
        if (r.status === 401) throw new TmdbError('La API key de TMDb no es válida.', true, 401)
        if (r.status === 429) throw new TmdbError('TMDb está limitando las peticiones. Inténtalo de nuevo en unos minutos.', true, 429)
        if (r.status !== 200) throw new TmdbError(`TMDb respondió con un error (${r.status}).`, r.status >= 500, r.status)
        return (await r.json()) as Json
      }
    })
  }

  /** Pide con es-MX y, si algún campo de texto viene vacío, completa solo esos campos con en-US. */
  private async conFallback(ruta: string, params: Record<string, string>, campos: string[]): Promise<Json> {
    const principal = await this.get(ruta, { ...params, language: IDIOMA_PRINCIPAL })
    const faltantes = campos.filter((c) => (texto(principal[c]) ?? '').trim() === '')
    if (faltantes.length === 0) return principal
    try {
      const respaldo = await this.get(ruta, { ...params, language: IDIOMA_FALLBACK })
      const combinado = { ...principal }
      for (const c of faltantes) combinado[c] = respaldo[c]
      return combinado
    } catch {
      return principal // sin red en el 2.º intento: mejor un campo vacío que romper toda la importación
    }
  }

  async buscar(apiKey: string, tipo: TipoBiblioteca, query: string, anio?: number | null): Promise<ResultadoBusqueda[]> {
    if (query.trim() === '') return []
    const pelicula = tipo === 'pelicula'
    const params: Record<string, string> = { api_key: apiKey, query, language: IDIOMA_PRINCIPAL, include_adult: 'false' }
    if (anio != null) params[pelicula ? 'year' : 'first_air_date_year'] = String(anio)
    const r = await this.get(pelicula ? 'search/movie' : 'search/tv', params)
    return ((r.results as Json[] | undefined) ?? []).map((x) => ({
      id: x.id as number,
      titulo: texto(pelicula ? x.title : x.name) ?? '',
      tituloOriginal: texto(pelicula ? x.original_title : x.original_name),
      anio: anioDeFecha(pelicula ? x.release_date : x.first_air_date),
      posterUrl: url(POSTER, x.poster_path),
      sinopsis: texto(x.overview)
    }))
  }

  async obtenerDetalle(apiKey: string, tipo: TipoBiblioteca, id: number): Promise<DetalleTmdb> {
    const pelicula = tipo === 'pelicula'
    const r = await this.conFallback(pelicula ? `movie/${id}` : `tv/${id}`, { api_key: apiKey, append_to_response: 'credits' }, [pelicula ? 'title' : 'name', 'overview'])
    const creditos = r.credits as Json | undefined
    let duracion: number | null = null
    if (pelicula) {
      const min = r.runtime as number | null | undefined
      if (min && min > 0) duracion = min * 60
    } else {
      const eps = r.episode_run_time as number[] | undefined
      if (eps?.length) duracion = eps[0] * 60
    }
    const companias = ((r.production_companies as Json[] | undefined) ?? []).map((c) => texto(c.name)).filter((n): n is string => !!n)
    return {
      id: r.id as number,
      titulo: texto(pelicula ? r.title : r.name) ?? '',
      tituloOriginal: texto(pelicula ? r.original_title : r.original_name),
      idiomaOriginal: texto(r.original_language),
      anio: anioDeFecha(pelicula ? r.release_date : r.first_air_date),
      sinopsis: texto(r.overview),
      estudio: companias[0] ?? null,
      posterUrl: url(POSTER, r.poster_path),
      backdropUrl: url(BACKDROP, r.backdrop_path),
      duracionSegundos: duracion,
      generos: ((r.genres as Json[] | undefined) ?? []).map((g) => g.name as string),
      reparto: elenco(creditos),
      equipo: equipo(creditos)
    }
  }

  /** Una sola petición por temporada: TMDb incluye en ella todos los episodios. */
  async obtenerTemporada(apiKey: string, seriesTmdbId: number, numero: number): Promise<TemporadaTmdb> {
    const r = await this.conFallback(`tv/${seriesTmdbId}/season/${numero}`, { api_key: apiKey, append_to_response: 'credits' }, ['name', 'overview'])
    const creditos = r.credits as Json | undefined
    return {
      tmdbId: (r.id as number | undefined) ?? null,
      nombre: texto(r.name),
      sinopsis: texto(r.overview),
      posterUrl: url(POSTER, r.poster_path),
      episodios: ((r.episodes as Json[] | undefined) ?? []).map((e) => {
        const min = e.runtime as number | null | undefined
        return {
          tmdbId: (e.id as number | undefined) ?? null,
          numeroEpisodio: e.episode_number as number,
          nombre: texto(e.name),
          sinopsis: texto(e.overview),
          stillUrl: url(STILL, e.still_path),
          duracionSegundos: min && min > 0 ? min * 60 : null
        }
      }),
      reparto: elenco(creditos),
      equipo: equipo(creditos)
    }
  }

  /** Comprueba una API key con una petición liviana. */
  async probarClave(apiKey: string): Promise<void> {
    await this.get('configuration', { api_key: apiKey })
  }
}

function elenco(creditos: Json | undefined): Persona[] {
  return ((creditos?.cast as Json[] | undefined) ?? []).map((c) => ({
    id: (c.id as number | undefined) ?? null,
    nombre: c.name as string,
    rol: texto(c.character) || null,
    fotoUrl: url(PERFIL, c.profile_path)
  }))
}

function equipo(creditos: Json | undefined): Persona[] {
  const vistos = new Set<number>()
  const salida: Persona[] = []
  for (const c of (creditos?.crew as Json[] | undefined) ?? []) {
    const etiqueta = ROLES_EQUIPO[c.job as string]
    const id = c.id as number | undefined
    if (!etiqueta || id == null || vistos.has(id)) continue
    vistos.add(id)
    salida.push({ id, nombre: c.name as string, rol: etiqueta, fotoUrl: url(PERFIL, c.profile_path) })
  }
  return salida
}
