import BetterSqlite3 from 'better-sqlite3'
import { existsSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import { parse } from 'yaml'
import type { ResumenCarpeta } from '../../shared/cliente-types'
import { esEscribible } from '../config/config-service'

/**
 * Qué hay en una carpeta de datos elegida en el asistente (specs/03 §2, camino A). Si ya contiene `cinemalove.sqlite`
 * (p. ej. de la versión Flutter) se resume para mostrar «Encontramos una colección existente». Solo lee: nunca modifica.
 */
export function inspeccionarCarpeta(ruta: string): ResumenCarpeta {
  const existe = existsSync(ruta)
  const escribible = existe && esEscribible(ruta)
  const archivo = join(ruta, 'cinemalove.sqlite')
  if (!existe || !existsSync(archivo)) return { existe, coleccion: null, escribible }
  let db: BetterSqlite3.Database | null = null
  try {
    db = new BetterSqlite3(archivo, { readonly: true, fileMustExist: true })
    const n = (sql: string): number => (db!.prepare(sql).get() as { n: number }).n
    return {
      existe,
      escribible,
      coleccion: {
        bibliotecas: n('SELECT COUNT(*) n FROM libraries'),
        peliculas: n("SELECT COUNT(*) n FROM media_items WHERE series_id IS NULL AND estado_archivo = 'ok'"),
        series: n("SELECT COUNT(*) n FROM series WHERE estado_archivo = 'ok'")
      }
    }
  } catch {
    // Archivo que no es una base de CinemaLove (o está bloqueado): se trata como «sin colección» y que abrirla dé el error claro.
    return { existe, coleccion: null, escribible }
  } finally {
    db?.close()
  }
}

/** `database_directory` del `config.yaml` de una instalación Flutter (atajo «Usar los datos de mi versión anterior»). */
export function datosDeConfigFlutter(rutaConfig: string): string | null {
  try {
    const doc = parse(readFileSync(rutaConfig, 'utf8')) as Record<string, unknown> | null
    const dir = doc?.database_directory
    if (typeof dir !== 'string' || !dir.trim()) return null
    return existsSync(dir) ? dir : null
  } catch {
    return null
  }
}

/** Carpeta de datos que se sugiere por defecto: `CinemaLoveData` junto al ejecutable. */
export const carpetaDatosSugerida = (carpetaConfig: string): string => join(carpetaConfig, 'CinemaLoveData')

