import { appendFileSync, mkdirSync, renameSync, rmSync, statSync } from 'node:fs'
import { join } from 'node:path'
import { format } from 'node:util'

export type NivelLog = 'info' | 'warn' | 'error'

const MAX_BYTES = 1_000_000
const ARCHIVOS = 3 // cinemalove.log + .1 + .2

/** Quita credenciales de lo que se escribe: `api_key=…`, `X-Api-Key: …`, tokens largos en hexadecimal y contraseñas en JSON. */
export function redactar(texto: string): string {
  return texto
    .replace(/(api[_-]?key["']?\s*[=:]\s*["']?)[^\s"'&,}]+/gi, '$1***')
    .replace(/("(?:password|contrasena|token|apiKey)"\s*:\s*")[^"]*(")/gi, '$1***$2')
    .replace(/\b[0-9a-f]{32,}\b/gi, '***')
}

/**
 * Registro en archivo (`<userData>/logs/cinemalove.log`) para diagnosticar problemas sin abrir una consola. Rota a 1 MB y guarda
 * 3 archivos, nunca falla (un error al escribir no debe tumbar la app) y no escribe credenciales.
 */
export class Registro {
  readonly archivo: string
  constructor(readonly carpeta: string) {
    this.archivo = join(carpeta, 'cinemalove.log')
    try {
      mkdirSync(carpeta, { recursive: true })
    } catch {
      /* sin carpeta no hay log, y no pasa nada */
    }
  }

  escribir(nivel: NivelLog, ...args: unknown[]): void {
    try {
      this.rotarSiHaceFalta()
      const linea = `${new Date().toISOString()} ${nivel.toUpperCase().padEnd(5)} ${redactar(format(...args))}\n`
      appendFileSync(this.archivo, linea, 'utf8')
    } catch {
      /* ver arriba */
    }
  }

  private rotarSiHaceFalta(): void {
    try {
      if (statSync(this.archivo).size < MAX_BYTES) return
    } catch {
      return // todavía no existe
    }
    rmSync(`${this.archivo}.${ARCHIVOS - 1}`, { force: true })
    for (let i = ARCHIVOS - 2; i >= 1; i--) {
      try {
        renameSync(`${this.archivo}.${i}`, `${this.archivo}.${i + 1}`)
      } catch {
        /* no existía */
      }
    }
    renameSync(this.archivo, `${this.archivo}.1`)
  }
}
