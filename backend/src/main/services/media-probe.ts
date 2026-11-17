import { execFile } from 'node:child_process'
import { statSync } from 'node:fs'
import type Database from 'better-sqlite3'
import { ahoraDb } from '../db/codecs'
import { parsearFfprobe, type FfprobeJson, type ProbeInterno } from './ffprobe-parse'

/** Ejecuta un binario y devuelve su stdout (inyectable para probar sin ffprobe real). */
export type Ejecutar = (comando: string, args: string[], opciones?: { timeoutMs?: number }) => Promise<string>

export const ejecutarProceso: Ejecutar = (comando, args, opciones) =>
  new Promise((resolver, rechazar) => {
    execFile(comando, args, { timeout: opciones?.timeoutMs ?? 30_000, maxBuffer: 32 * 1024 * 1024, windowsHide: true }, (err, stdout, stderr) => {
      if (err) rechazar(new Error(`${comando} falló: ${(stderr || err.message).toString().trim().split('\n').slice(-3).join(' ')}`))
      else resolver(stdout.toString())
    })
  })

export type TipoObjetivo = 'item' | 'extra'

/**
 * Sonda de archivos con ffprobe. El resultado resumido se cachea en `cl_media_probe` y se invalida si cambian el
 * tamaño o la fecha de modificación del archivo (specs/02 §6).
 */
export class MediaProbeService {
  constructor(
    private readonly ffprobe: string,
    private readonly db: Database.Database | null,
    private readonly ejecutar: Ejecutar = ejecutarProceso
  ) {}

  async sondear(tipo: TipoObjetivo, id: string, rutaArchivo: string): Promise<ProbeInterno> {
    const st = statSync(rutaArchivo)
    const mtimeMs = Math.floor(st.mtimeMs)

    const guardado = this.db?.prepare('SELECT tamano_bytes, mtime_ms, probe_json FROM cl_media_probe WHERE objetivo_tipo = ? AND objetivo_id = ?').get(tipo, id) as
      | { tamano_bytes: number; mtime_ms: number; probe_json: string }
      | undefined
    if (guardado && guardado.tamano_bytes === st.size && guardado.mtime_ms === mtimeMs) {
      try {
        return JSON.parse(guardado.probe_json) as ProbeInterno
      } catch {
        // caché corrupta: se vuelve a sondear
      }
    }

    const crudo = await this.ejecutar(this.ffprobe, ['-v', 'error', '-print_format', 'json', '-show_format', '-show_streams', rutaArchivo])
    const probe = parsearFfprobe(JSON.parse(crudo) as FfprobeJson, rutaArchivo, st.size)

    try {
      this.db
        ?.prepare('INSERT OR REPLACE INTO cl_media_probe (objetivo_tipo, objetivo_id, tamano_bytes, mtime_ms, probe_json, creado_en) VALUES (?, ?, ?, ?, ?, ?)')
        .run(tipo, id, st.size, mtimeMs, JSON.stringify(probe), ahoraDb())
    } catch {
      // base en solo lectura: se sondea cada vez, sin romper la reproducción
    }
    return probe
  }
}
