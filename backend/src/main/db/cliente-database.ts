import BetterSqlite3 from 'better-sqlite3'
import { existsSync } from 'node:fs'
import { join } from 'node:path'
import schemaCliente from './schema-cliente-v6.sql?raw'

/** `user_version` de `cliente_datos.sqlite` en la versión Flutter (specs/02 §7). */
export const VERSION_ESQUEMA_CLIENTE = 6

export interface BaseCliente {
  db: BetterSqlite3.Database
  cerrar(): void
}

/**
 * Abre (o crea) `cliente_datos.sqlite`: el estado de ESTE equipo como cliente. Reglas de specs/02 §2: solo cambios aditivos
 * (`cl_*`); una base de una versión más antigua o más nueva que la conocida no se toca.
 */
export function abrirBaseCliente(carpeta: string): BaseCliente {
  const ruta = join(carpeta, 'cliente_datos.sqlite')
  const existia = existsSync(ruta)
  const db = new BetterSqlite3(ruta)
  try {
    db.pragma('foreign_keys = ON')
    db.pragma('busy_timeout = 5000')
    if (!existia) {
      db.transaction(() => {
        db.exec(schemaCliente)
        db.pragma(`user_version = ${VERSION_ESQUEMA_CLIENTE}`)
      })()
    } else {
      const v = db.pragma('user_version', { simple: true }) as number
      if (v !== VERSION_ESQUEMA_CLIENTE) throw new Error(`cliente_datos.sqlite tiene una versión de esquema (${v}) que esta versión no conoce.`)
    }
    // Aditivo y propio de Electron: lo que Flutter no guarda de cada servidor remoto.
    db.exec(`CREATE TABLE IF NOT EXISTS cl_cliente_servidor_extra (
      servidor_id        TEXT PRIMARY KEY REFERENCES cliente_servidores (id) ON DELETE CASCADE,
      nombre_servidor    TEXT,
      server_uuid        TEXT,
      huella_cert_sha256 TEXT,
      cert_pem           TEXT
    )`)
  } catch (e) {
    db.close()
    throw e
  }
  return { db, cerrar: () => db.close() }
}
