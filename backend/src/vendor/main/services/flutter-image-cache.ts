import { existsSync, readFileSync } from 'node:fs'
import { join } from 'node:path'

interface EntradaIndice {
  url?: unknown
  key?: unknown
  relativePath?: unknown
}

/**
 * Índice de solo lectura de `images/imageCacheInfo.json` (flutter_cache_manager).
 * Nunca se escribe: lo gestiona Flutter.
 */
export class FlutterImageCache {
  private readonly mapa = new Map<string, string>()

  constructor(private readonly carpetaImages: string) {
    this.cargar()
  }

  private cargar(): void {
    const ruta = join(this.carpetaImages, 'imageCacheInfo.json')
    if (!existsSync(ruta)) return
    let datos: unknown
    try {
      datos = JSON.parse(readFileSync(ruta, 'utf8'))
    } catch {
      return
    }
    if (!Array.isArray(datos)) return
    for (const e of datos as EntradaIndice[]) {
      const url = typeof e.url === 'string' ? e.url : typeof e.key === 'string' ? e.key : null
      if (url && typeof e.relativePath === 'string') this.mapa.set(url, e.relativePath)
    }
  }

  get tamano(): number {
    return this.mapa.size
  }

  /** Ruta absoluta del archivo cacheado para una URL de TMDb, o null si no está o no existe. */
  resolver(url: string): string | null {
    const rel = this.mapa.get(url)
    if (!rel) return null
    const abs = join(this.carpetaImages, rel)
    return existsSync(abs) ? abs : null
  }
}
