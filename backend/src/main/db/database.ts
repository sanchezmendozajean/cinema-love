import BetterSqlite3 from 'better-sqlite3'
import { copyFileSync, existsSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs'
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
  /** `false` desactiva el respaldo automático (solo pruebas). */
  respaldar?: boolean
}

export interface BaseAbierta {
  db: BetterSqlite3.Database
  soloLectura: boolean
  creada: boolean
  /** Aviso para mostrar al usuario (p. ej. versión Flutter más nueva). */
  aviso: string | null
  /** Respaldo hecho al abrir (nombre de archivo) o null. */
  respaldo: string | null
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

// Bloqueo de un solo escritor entre las tres versiones. Electron crea `cinemalove.electron.lock` y comprueba solo ese
// nombre, así que esta versión lo respeta y TAMBIÉN lo crea (para que Electron detecte que estamos abiertos), además de
// su propio `cinemalove.qt.lock`. Flutter no escribe bloqueo: no se puede detectar, hay que no abrirlo a la vez.
const LOCKS = ['cinemalove.electron.lock', 'cinemalove.qt.lock']

function tomarLocks(carpeta: string): void {
  for (const nombre of LOCKS) {
    const ruta = join(carpeta, nombre)
    if (!existsSync(ruta)) continue
    const pid = Number(readFileSync(ruta, 'utf8').trim())
    if (Number.isInteger(pid) && pid !== process.pid && procesoVivo(pid)) {
      throw new ErrorApertura(
        'bloqueada',
        'Otra instancia de CinemaLove (o una de sus otras versiones) tiene abiertos estos datos. Ciérrala e inténtalo de nuevo.'
      )
    }
  }
  for (const nombre of LOCKS) writeFileSync(join(carpeta, nombre), String(process.pid), 'utf8')
}

/** Quita solo los bloqueos que son nuestros (nunca el de otra aplicación). */
function soltarLocks(carpeta: string): void {
  for (const nombre of LOCKS) {
    const ruta = join(carpeta, nombre)
    try {
      if (existsSync(ruta) && Number(readFileSync(ruta, 'utf8').trim()) === process.pid) rmSync(ruta, { force: true })
    } catch {
      // carpeta ya no disponible: nada que soltar
    }
  }
}

const PREFIJO_RESPALDO = 'cinemalove.sqlite.bak-qt-'
const DIAS_ENTRE_RESPALDOS = 7
const RESPALDOS_A_CONSERVAR = 5

function respaldosQt(carpeta: string): string[] {
  return readdirSync(carpeta).filter((f) => f.startsWith(PREFIJO_RESPALDO)).sort()
}

function aaaammdd(d = new Date()): string {
  return d.toISOString().slice(0, 10).replaceAll('-', '')
}

/**
 * Respaldo automático al abrir una base que ya existe: si no hay ninguno, o el último tiene más de 7 días, se crea
 * `cinemalove.sqlite.bak-qt-AAAAMMDD` (copia consistente con VACUUM INTO; si falla, copia del archivo). Se conservan los
 * últimos 5. Nunca se mueve ni se modifica la base original. Devuelve el nombre creado o null.
 */
function respaldarSiHaceFalta(carpeta: string, ruta: string): string | null {
  const previos = respaldosQt(carpeta)
  const ultimo = previos.at(-1)
  if (ultimo) {
    const edadDias = (Date.now() - statSync(join(carpeta, ultimo)).mtimeMs) / 86_400_000
    if (edadDias < DIAS_ENTRE_RESPALDOS) return null
  }
  let nombre = `${PREFIJO_RESPALDO}${aaaammdd()}`
  if (existsSync(join(carpeta, nombre))) nombre += `-${Date.now() % 100000}`
  const destino = join(carpeta, nombre)
  try {
    const origen = new BetterSqlite3(ruta, { readonly: true })
    try {
      origen.exec(`VACUUM INTO '${destino.replaceAll("'", "''")}'`)
    } finally {
      origen.close()
    }
  } catch {
    rmSync(destino, { force: true })
    copyFileSync(ruta, destino)
  }
  for (const viejo of respaldosQt(carpeta).slice(0, -RESPALDOS_A_CONSERVAR)) rmSync(join(carpeta, viejo), { force: true })
  return nombre
}

/** Abre `cinemalove.sqlite` aplicando las reglas de specs/02 §2. */
export function abrirBase(carpeta: string, opciones: AbrirOpciones = {}): BaseAbierta {
  const ruta = join(carpeta, 'cinemalove.sqlite')
  const existia = existsSync(ruta)
  tomarLocks(carpeta)

  const liberar = (): void => soltarLocks(carpeta)
  try {
    const respaldo = existia && opciones.respaldar !== false ? respaldarSiHaceFalta(carpeta, ruta) : null

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
      respaldo,
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
