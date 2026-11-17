import { existsSync } from 'node:fs'
import { join } from 'node:path'

export interface RutasFfmpeg {
  ffmpeg: string
  ffprobe: string
}

/** Carpeta `<plataforma>-<arch>` de los binarios empaquetados (la misma que usa `fetch:ffmpeg`). */
export function carpetaPlataforma(plataforma = process.platform, arch = process.arch): string {
  const so = plataforma === 'win32' ? 'win' : plataforma
  return `${so}-${arch}`
}

/**
 * Ubica ffmpeg/ffprobe: empaquetados en `resources/ffmpeg` (producción) o en `resources/ffmpeg/<plataforma>` del
 * repositorio (desarrollo). Nunca se usa el ffmpeg del sistema: los builds son autocontenidos.
 */
export function resolverFfmpeg(opts: { empaquetada: boolean; resourcesPath: string; raizProyecto: string }): RutasFfmpeg | null {
  const exe = process.platform === 'win32' ? '.exe' : ''
  const carpeta = opts.empaquetada ? join(opts.resourcesPath, 'ffmpeg') : join(opts.raizProyecto, 'resources', 'ffmpeg', carpetaPlataforma())
  const r = { ffmpeg: join(carpeta, `ffmpeg${exe}`), ffprobe: join(carpeta, `ffprobe${exe}`) }
  return existsSync(r.ffmpeg) && existsSync(r.ffprobe) ? r : null
}
