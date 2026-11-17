/** Minúsculas y sin acentos, para búsquedas ("Pelicula" encuentra "Película"). */
export function normalizarTexto(s: string): string {
  return s.normalize('NFD').replace(/\p{M}+/gu, '').toLowerCase().trim()
}

const MAX_MEMO = 50_000

/**
 * Versión memoizada para la función SQL `unaccent_lower`: el catálogo la invoca por fila y en
 * varias consultas por petición, y normalizar es lo más caro de esas consultas.
 */
export function crearNormalizadorMemoizado(): (v: unknown) => string {
  const memo = new Map<string, string>()
  return (v) => {
    if (typeof v !== 'string') return ''
    let r = memo.get(v)
    if (r === undefined) {
      if (memo.size >= MAX_MEMO) memo.clear()
      r = normalizarTexto(v)
      memo.set(v, r)
    }
    return r
  }
}

const EXTENSION_VIDEO = /\.(mp4|mkv|avi|mov|m4v|wmv|flv|webm|ts|mpg|mpeg)$/i

/** Nombre legible derivado de una ruta relativa: último segmento, sin extensión y con puntos/guiones bajos como espacios. */
export function nombreDeArchivo(ruta: unknown): string {
  if (typeof ruta !== 'string') return ''
  const ultimo = ruta.split(/[\\/]/).filter(Boolean).at(-1) ?? ''
  return ultimo.replace(EXTENSION_VIDEO, '').replace(/[._]+/g, ' ').replace(/\s+/g, ' ').trim()
}
