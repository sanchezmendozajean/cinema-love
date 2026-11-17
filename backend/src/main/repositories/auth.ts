import type Database from 'better-sqlite3'
import { randomBytes } from 'node:crypto'
import { ahoraDb } from '../db/codecs'

const DURACION_SESION_S = 30 * 24 * 3600

export interface UsuarioDb {
  id: string
  nombreUsuario: string
  passwordHash: string
}

/** Credenciales y sesiones (tablas `api_keys`, `usuarios`, `sesiones`) + metadatos del servidor. */
export class AuthRepo {
  constructor(private readonly db: Database.Database) {}

  apiKeyActiva(): string | null {
    const r = this.db.prepare('SELECT valor FROM api_keys WHERE activa = 1 LIMIT 1').get() as
      | { valor: string }
      | undefined
    return r?.valor ?? null
  }

  usuarioPorNombre(nombre: string): UsuarioDb | null {
    const r = this.db
      .prepare('SELECT id, nombre_usuario, password_hash FROM usuarios WHERE nombre_usuario = ?')
      .get(nombre) as { id: string; nombre_usuario: string; password_hash: string } | undefined
    return r ? { id: r.id, nombreUsuario: r.nombre_usuario, passwordHash: r.password_hash } : null
  }

  /** Usuario dueño de un token vigente; borra el token si ya expiró (limpieza perezosa, como Flutter). */
  usuarioDeToken(token: string): { id: string; nombreUsuario: string } | null {
    const s = this.db.prepare('SELECT usuario_id, expira_en FROM sesiones WHERE token = ?').get(token) as
      | { usuario_id: string; expira_en: number }
      | undefined
    if (!s) return null
    if (s.expira_en <= ahoraDb()) {
      this.invalidarSesion(token)
      return null
    }
    const u = this.db.prepare('SELECT id, nombre_usuario FROM usuarios WHERE id = ?').get(s.usuario_id) as
      | { id: string; nombre_usuario: string }
      | undefined
    return u ? { id: u.id, nombreUsuario: u.nombre_usuario } : null
  }

  crearSesion(usuarioId: string): string {
    const token = randomBytes(32).toString('hex')
    const ahora = ahoraDb()
    this.db
      .prepare('INSERT INTO sesiones (token, usuario_id, creada_en, expira_en) VALUES (?, ?, ?, ?)')
      .run(token, usuarioId, ahora, ahora + DURACION_SESION_S)
    return token
  }

  invalidarSesion(token: string): void {
    this.db.prepare('DELETE FROM sesiones WHERE token = ?').run(token)
  }

  serverId(): string {
    const r = this.db.prepare("SELECT valor FROM cl_meta WHERE clave = 'server_id'").get() as
      | { valor: string }
      | undefined
    return r?.valor ?? 'desconocido'
  }

  guardarAjuste(clave: string, valor: string | null): void {
    if (valor === null) this.db.prepare('DELETE FROM app_settings WHERE clave = ?').run(clave)
    else this.db.prepare('INSERT INTO app_settings (clave, valor) VALUES (?, ?) ON CONFLICT(clave) DO UPDATE SET valor = excluded.valor').run(clave, valor)
  }

  ajuste(clave: string): string | null {
    const r = this.db.prepare('SELECT valor FROM app_settings WHERE clave = ?').get(clave) as
      | { valor: string }
      | undefined
    return r?.valor ?? null
  }
}
