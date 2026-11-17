import { ESTADOS_ARCHIVO, ESTADOS_VISTO, TIPOS_BIBLIOTECA } from '../../shared/enums'
import type { EstadoArchivo, EstadoVisto, TipoBiblioteca } from '../../shared/enums'

/** Fechas: INTEGER, epoch en segundos (UTC), como drift. */
export const fechaDesdeDb = (s: number): Date => new Date(s * 1000)
export const fechaADb = (d: Date): number => Math.floor(d.getTime() / 1000)
export const ahoraDb = (): number => Math.floor(Date.now() / 1000)
export const fechaNullDesdeDb = (s: number | null): Date | null => (s == null ? null : fechaDesdeDb(s))

/** Booleanos: INTEGER 0/1. */
export const boolDesdeDb = (v: number): boolean => v !== 0
export const boolADb = (b: boolean): number => (b ? 1 : 0)

function enumDesdeDb<T extends string>(valores: readonly T[], v: string, nombre: string): T {
  if ((valores as readonly string[]).includes(v)) return v as T
  throw new Error(`Valor de ${nombre} desconocido en la base: "${v}"`)
}
export const tipoBibliotecaDesdeDb = (v: string): TipoBiblioteca =>
  enumDesdeDb(TIPOS_BIBLIOTECA, v, 'tipo de biblioteca')
export const estadoArchivoDesdeDb = (v: string): EstadoArchivo =>
  enumDesdeDb(ESTADOS_ARCHIVO, v, 'estado de archivo')
export const estadoVistoDesdeDb = (v: string): EstadoVisto =>
  enumDesdeDb(ESTADOS_VISTO, v, 'estado de visto')

/** Listas de géneros: TEXT con JSON `["Drama","Crimen"]`. */
export function generosDesdeDb(v: string | null): string[] {
  if (!v) return []
  try {
    const x: unknown = JSON.parse(v)
    return Array.isArray(x) ? x.filter((g): g is string => typeof g === 'string') : []
  } catch {
    return []
  }
}
export const generosADb = (g: string[]): string | null => (g.length ? JSON.stringify(g) : null)

export interface Persona {
  id: number | null
  nombre: string
  rol: string | null
  fotoUrl: string | null
}

/** Reparto/equipo: JSON `[{"id","nombre","rol","fotoUrl"}]`; se lee también la clave legado `personaje`. */
export function personasDesdeDb(v: string | null): Persona[] {
  if (!v) return []
  let x: unknown
  try {
    x = JSON.parse(v)
  } catch {
    return []
  }
  if (!Array.isArray(x)) return []
  const out: Persona[] = []
  for (const o of x as Record<string, unknown>[]) {
    if (!o || typeof o !== 'object' || typeof o.nombre !== 'string') continue
    const rol = typeof o.rol === 'string' ? o.rol : typeof o.personaje === 'string' ? o.personaje : null
    out.push({
      id: typeof o.id === 'number' ? o.id : null,
      nombre: o.nombre,
      rol,
      fotoUrl: typeof o.fotoUrl === 'string' ? o.fotoUrl : null
    })
  }
  return out
}
export const personasADb = (p: Persona[]): string | null =>
  p.length
    ? JSON.stringify(p.map((x) => ({ id: x.id, nombre: x.nombre, rol: x.rol, fotoUrl: x.fotoUrl })))
    : null
