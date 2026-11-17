import { createHash } from 'node:crypto'
import { existsSync, mkdirSync, statSync, writeFileSync } from 'node:fs'
import { basename, extname, join } from 'node:path'
import type Database from 'better-sqlite3'
import { ahoraDb } from '../db/codecs'
import type { FlutterImageCache } from './flutter-image-cache'

const ORIGEN_TMDB = 'https://image.tmdb.org/'
const TAMANOS = /^(w\d{2,4}|original)$/
const TIPOS: Record<string, string> = {
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.png': 'image/png',
  '.webp': 'image/webp'
}

export interface ImagenResuelta {
  ruta: string
  contentType: string
}

export const esUrlDeTmdb = (url: string): boolean => url.startsWith(ORIGEN_TMDB) && !url.includes('..')

/** Cambia el segmento de tamaño (`/t/p/w500/` → `/t/p/w342/`). */
export function conTamano(url: string, tamano: string): string {
  return url.replace(/\/t\/p\/(w\d+|original)\//, `/t/p/${tamano}/`)
}

/**
 * Resolución (specs/02 §8): índice de Flutter → `cl_image_cache` → descarga de TMDb → null.
 * Las descargas van a `images/cl/<sha1(url)>.<ext>`; el índice de Flutter nunca se escribe.
 */
export class ImageService {
  private readonly enCurso = new Map<string, Promise<ImagenResuelta | null>>()

  constructor(
    private readonly carpetaImages: string,
    private readonly flutter: FlutterImageCache,
    private readonly db: Database.Database | null,
    private readonly descargar: typeof fetch = fetch
  ) {}

  async obtener(src: string, tamano?: string): Promise<ImagenResuelta | null> {
    if (!esUrlDeTmdb(src)) return null
    if (tamano !== undefined && !TAMANOS.test(tamano)) return null
    const url = tamano ? conTamano(src, tamano) : src

    const local = this.buscarLocal(url)
    if (local) return local

    const previa = this.enCurso.get(url)
    if (previa) return previa
    const p = this.bajar(url).finally(() => this.enCurso.delete(url))
    this.enCurso.set(url, p)
    return p
  }

  private buscarLocal(url: string): ImagenResuelta | null {
    const flutter = this.flutter.resolver(url)
    if (flutter) return { ruta: flutter, contentType: TIPOS[extname(flutter).toLowerCase()] ?? 'image/jpeg' }

    const fila = this.db?.prepare('SELECT ruta_relativa FROM cl_image_cache WHERE url = ?').get(url) as
      | { ruta_relativa: string }
      | undefined
    if (fila) {
      const ruta = join(this.carpetaImages, fila.ruta_relativa)
      if (existsSync(ruta)) return { ruta, contentType: TIPOS[extname(ruta).toLowerCase()] ?? 'image/jpeg' }
    }
    // Descargas previas: la ruta es determinística, así que no depende de que cl_image_cache se haya podido escribir.
    const ruta = this.rutaCl(url)
    return existsSync(ruta) ? { ruta, contentType: TIPOS[extname(ruta)] ?? 'image/jpeg' } : null
  }

  private rutaCl(url: string): string {
    const ext = extname(new URL(url).pathname).toLowerCase() || '.jpg'
    return join(this.carpetaImages, 'cl', `${createHash('sha1').update(url).digest('hex')}${ext}`)
  }

  private async bajar(url: string): Promise<ImagenResuelta | null> {
    try {
      const r = await this.descargar(url, { signal: AbortSignal.timeout(15_000) })
      if (!r.ok) return null
      const ext = extname(new URL(url).pathname).toLowerCase() || '.jpg'
      const contentType = TIPOS[ext]
      if (!contentType) return null
      const datos = Buffer.from(await r.arrayBuffer())
      const ruta = this.rutaCl(url)
      const rel = `cl/${basename(ruta)}`
      mkdirSync(join(this.carpetaImages, 'cl'), { recursive: true })
      writeFileSync(ruta, datos)
      try {
        this.db
          ?.prepare('INSERT OR REPLACE INTO cl_image_cache (url, ruta_relativa, bytes, creado_en) VALUES (?, ?, ?, ?)')
          .run(url, rel, statSync(ruta).size, ahoraDb())
      } catch {
        // Base en solo lectura: la imagen queda en disco y se encuentra por su ruta determinística.
      }
      return { ruta, contentType }
    } catch {
      return null // sin red: el cliente muestra el placeholder
    }
  }
}
