import BetterSqlite3 from 'better-sqlite3'
import { copyFileSync, existsSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'
import schemaV11 from './schema-v11.sql?raw'
import { aplicarMigraciones } from './migrations'
import { crearNormalizadorMemoizado, nombreDeArchivo } from './texto'

export const VERSION_ESQUEMA_DRIFT = 11

export type ErrorAperturaCodigo = 'version-antigua' | 'bloqueada'

export class ErrorApertura extends Error {
  constructor(
    readonly codigo: ErrorAperturaCodigo,
    mensaje: string
  ) {
    super(mensaje)
  }
}

export interface AbrirOpciones {
  /** Si hay un `.bak-electron-*` previo no se vuelve a respaldar. */
  respaldar?: boolean
}

export interface BaseAbierta {
  db: BetterSqlite3.Database
  soloLectura: boolean
  creada: boolean
  /** Aviso para mostrar al usuario (p. ej. versión Flutter más nueva). */
  aviso: string | null
  cerrar(): void
}

function procesoVivo(pid: number): boolean {
  try {
    process.kill(pid, 0)
    return true
  } catch (e) {
    return (e as NodeJS.ErrnoException).code === 'EPERM'
  }
}

function tomarLock(rutaLock: string): void {
  if (existsSync(rutaLock)) {
    const pid = Number(readFileSync(rutaLock, 'utf8').trim())
    if (Number.isInteger(pid) && pid !== process.pid && procesoVivo(pid)) {
      throw new ErrorApertura(
        'bloqueada',
        'Otra instancia de CinemaLove (o la versión Flutter) tiene abiertos estos datos. Ciérrala e inténtalo de nuevo.'
      )
    }
  }
  writeFileSync(rutaLock, String(process.pid), 'utf8')
}

function respaldoPrevio(carpeta: string): boolean {
  return readdirSync(carpeta).some((f) => f.startsWith('cinemalove.sqlite.bak-electron-'))
}

function aaaammdd(d = new Date()): string {
  return d.toISOString().slice(0, 10).replaceAll('-', '')
}

/** Abre `cinemalove.sqlite` aplicando las reglas de specs/02 §2. */
export function abrirBase(carpeta: string, opciones: AbrirOpciones = {}): BaseAbierta {
  const ruta = join(carpeta, 'cinemalove.sqlite')
  const rutaLock = join(carpeta, 'cinemalove.electron.lock')
  const existia = existsSync(ruta)
  tomarLock(rutaLock)

  const liberar = (): void => rmSync(rutaLock, { force: true })
  try {
    if (existia && opciones.respaldar !== false && !respaldoPrevio(carpeta)) {
      copyFileSync(ruta, `${ruta}.bak-electron-${aaaammdd()}`)
    }

    // Primero se mira el user_version en solo lectura, antes de decidir cómo abrir.
    let soloLectura = false
    let aviso: string | null = null
    if (existia) {
      const sonda = new BetterSqlite3(ruta, { readonly: true })
      const v = sonda.pragma('user_version', { simple: true }) as number
      sonda.close()
      if (v < VERSION_ESQUEMA_DRIFT) {
        throw new ErrorApertura(
          'version-antigua',
          'Esta base fue creada con una versión antigua de CinemaLove. Ábrela una vez con la versión Flutter para actualizarla.'
        )
      }
      if (v > VERSION_ESQUEMA_DRIFT) {
        soloLectura = true
        aviso = 'Esta base es de una versión más nueva de CinemaLove. Se abrió en solo lectura para no arriesgar tus datos.'
      }
    }

    const db = new BetterSqlite3(ruta, { readonly: soloLectura })
    db.pragma('foreign_keys = ON')
    db.pragma('busy_timeout = 5000')
    db.function('nombre_archivo', { deterministic: true }, (v) => nombreDeArchivo(v))
    db.function('unaccent_lower', { deterministic: true }, crearNormalizadorMemoizado())
    // journal_mode: se deja el que tenga el archivo.

    if (!existia) {
      db.transaction(() => {
        db.exec(schemaV11)
        db.pragma(`user_version = ${VERSION_ESQUEMA_DRIFT}`)
      })()
    }
    if (!soloLectura) aplicarMigraciones(db)

    return {
      db,
      soloLectura,
      creada: !existia,
      aviso,
      cerrar: () => {
        db.close()
        liberar()
      }
    }
  } catch (e) {
    liberar()
    throw e
  }
}
