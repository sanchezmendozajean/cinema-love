export interface PersonaTmdb {
  nombre: string | null
  biografia: string | null
  nacimiento: string | null
  fallecimiento: string | null
  lugarNacimiento: string | null
  fotoUrl: string | null
  departamento: string | null
}

const IDIOMAS = ['es-MX', 'en-US'] as const
const DEPARTAMENTOS: Record<string, string> = {
  Acting: 'Actuación',
  Directing: 'Dirección',
  Writing: 'Guion',
  Production: 'Producción',
  Camera: 'Fotografía',
  Sound: 'Sonido',
  Editing: 'Montaje'
}

/**
 * Biografía de una persona desde TMDb (specs/04 «Personas»): es-MX con fallback a en-US cuando la
 * biografía viene vacía. Cacheada en memoria; sin clave o sin red devuelve null y la ficha se arma
 * solo con lo que hay en el catálogo.
 */
export class PersonaService {
  private readonly cache = new Map<number, PersonaTmdb | null>()

  constructor(
    private readonly apiKey: () => string | null,
    private readonly pedir: typeof fetch = fetch
  ) {}

  async obtener(tmdbId: number): Promise<PersonaTmdb | null> {
    if (this.cache.has(tmdbId)) return this.cache.get(tmdbId) ?? null
    const key = this.apiKey()
    if (!key) return null

    let resultado = null as PersonaTmdb | null
    for (const idioma of IDIOMAS) {
      const d = await this.consultar(tmdbId, idioma, key)
      if (!d) break // sin red / inexistente: no se cachea para poder reintentar luego
      const previa: string | null = resultado?.biografia ?? null
      resultado = {
        nombre: d.name ?? null,
        biografia: d.biography?.trim() || previa,
        nacimiento: d.birthday ?? null,
        fallecimiento: d.deathday ?? null,
        lugarNacimiento: d.place_of_birth ?? null,
        fotoUrl: d.profile_path ? `https://image.tmdb.org/t/p/w500${d.profile_path}` : null,
        departamento: d.known_for_department ? (DEPARTAMENTOS[d.known_for_department] ?? d.known_for_department) : null
      }
      if (resultado.biografia) break
    }
    if (resultado) this.cache.set(tmdbId, resultado)
    return resultado
  }

  private async consultar(
    id: number,
    idioma: string,
    key: string
  ): Promise<{
    name?: string
    biography?: string
    birthday?: string
    deathday?: string
    place_of_birth?: string
    profile_path?: string
    known_for_department?: string
  } | null> {
    try {
      const r = await this.pedir(`https://api.themoviedb.org/3/person/${id}?language=${idioma}&api_key=${encodeURIComponent(key)}`, {
        signal: AbortSignal.timeout(10_000)
      })
      return r.ok ? await r.json() : null
    } catch {
      return null
    }
  }
}
