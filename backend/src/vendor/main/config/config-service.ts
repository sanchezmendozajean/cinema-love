import { accessSync, constants, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { parse } from 'yaml'

export type ModoConfig = 'local' | 'cliente'

export interface ConfigYaml {
  modo: ModoConfig
  databaseDirectory: string | null
  servidorId: string | null
}

export interface EntornoRutas {
  empaquetada: boolean
  execPath: string
  env: Record<string, string | undefined>
  /** Carpeta de desarrollo (`.dev-data/`) usada cuando la app no está empaquetada. */
  devDataDir: string
  userData: string
}

/** Carpeta de `config.yaml` según la distribución (specs/02 §1). */
export function carpetaDeConfig(e: EntornoRutas): string {
  // Solo en desarrollo: `CL_CONFIG_DIR` permite probar el asistente y el Modo Cliente sin tocar el config.yaml real de .dev-data.
  if (!e.empaquetada) return e.env.CL_CONFIG_DIR || e.devDataDir
  if (e.env.PORTABLE_EXECUTABLE_DIR) return e.env.PORTABLE_EXECUTABLE_DIR
  if (e.env.APPIMAGE) return dirname(e.env.APPIMAGE)
  return dirname(e.execPath)
}

export function esEscribible(dir: string): boolean {
  try {
    accessSync(dir, constants.W_OK)
    return true
  } catch {
    return false
  }
}

/**
 * Lee y escribe `config.yaml` solo con las claves de Flutter (`database_directory`, `modo`,
 * `servidor_id`): Flutter reescribe el archivo entero, así que no hay claves propias aquí.
 */
export class ConfigService {
  readonly rutaConfig: string
  constructor(readonly carpeta: string) {
    this.rutaConfig = join(carpeta, 'config.yaml')
  }

  leer(): ConfigYaml {
    const vacio: ConfigYaml = { modo: 'local', databaseDirectory: null, servidorId: null }
    if (!existsSync(this.rutaConfig)) return vacio
    let doc: unknown
    try {
      doc = parse(readFileSync(this.rutaConfig, 'utf8'))
    } catch {
      return vacio
    }
    if (!doc || typeof doc !== 'object') return vacio
    const o = doc as Record<string, unknown>
    const str = (v: unknown): string | null => (typeof v === 'string' && v.trim() ? v : null)
    return {
      modo: o.modo === 'cliente' ? 'cliente' : 'local',
      databaseDirectory: str(o.database_directory),
      servidorId: str(o.servidor_id)
    }
  }

  /** Carpeta de datos: la de `config.yaml` en modo cliente, `database_directory` en modo local. */
  carpetaDeDatos(): string | null {
    const c = this.leer()
    return c.modo === 'cliente' ? this.carpeta : c.databaseDirectory
  }

  /** Borra `config.yaml`: la próxima vez se muestra el asistente de bienvenida. */
  borrar(): void {
    rmSync(this.rutaConfig, { force: true })
  }

  guardar(c: ConfigYaml): void {
    mkdirSync(this.carpeta, { recursive: true })
    const q = (s: string): string => JSON.stringify(s)
    const lineas =
      c.modo === 'cliente'
        ? ['modo: cliente', ...(c.servidorId ? [`servidor_id: ${q(c.servidorId)}`] : [])]
        : c.databaseDirectory
          ? [`database_directory: ${q(c.databaseDirectory)}`]
          : []
    writeFileSync(this.rutaConfig, lineas.join('\n') + '\n', 'utf8')
  }
}
