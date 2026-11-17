import { existsSync, readdirSync, rmSync, statSync } from 'node:fs'
import { join } from 'node:path'
import type Database from 'better-sqlite3'
import type { InfoDatos, TamanoCarpeta } from '../../shared/api-types'

function medir(carpeta: string): TamanoCarpeta {
  const r: TamanoCarpeta = { archivos: 0, bytes: 0 }
  if (!existsSync(carpeta)) return r
  const recorrer = (dir: string): void => {
    for (const e of readdirSync(dir, { withFileTypes: true })) {
      const ruta = join(dir, e.name)
      if (e.isDirectory()) recorrer(ruta)
      else {
        try {
          r.bytes += statSync(ruta).size
          r.archivos++
        } catch {
          /* desapareció mientras se medía */
        }
      }
    }
  }
  recorrer(carpeta)
  return r
}

export function infoDatos(carpeta: string): InfoDatos {
  let baseBytes = 0
  try {
    baseBytes = statSync(join(carpeta, 'cinemalove.sqlite')).size
  } catch {
    /* sin base todavía */
  }
  return { carpeta, imagenes: medir(join(carpeta, 'images', 'cl')), temporales: medir(join(carpeta, 'transcode')), baseBytes }
}

/**
 * Borra las imágenes descargadas por CinemaLove (`images/cl/`) y su índice. NO toca `images/` de Flutter. Es solo caché: se
 * vuelven a bajar de TMDb cuando hacen falta. Devuelve cuántos archivos y bytes liberó.
 */
export function limpiarImagenes(db: Database.Database | null, carpeta: string): TamanoCarpeta {
  const antes = medir(join(carpeta, 'images', 'cl'))
  rmSync(join(carpeta, 'images', 'cl'), { recursive: true, force: true })
  try {
    db?.prepare('DELETE FROM cl_image_cache').run()
  } catch {
    /* base en solo lectura: las imágenes se encuentran por su ruta y se vuelven a descargar */
  }
  return antes
}

/** Borra los temporales de reproducción (subtítulos convertidos). Se regeneran al reproducir. */
export function limpiarTemporales(carpeta: string): TamanoCarpeta {
  const ruta = join(carpeta, 'transcode')
  const antes = medir(ruta)
  if (existsSync(ruta)) for (const f of readdirSync(ruta)) rmSync(join(ruta, f), { recursive: true, force: true })
  return antes
}
