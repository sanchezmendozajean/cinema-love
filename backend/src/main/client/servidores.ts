import { randomUUID } from 'node:crypto'
import type Database from 'better-sqlite3'

import type { TipoCredencial } from '../../shared/cliente-types'
import { EstadoLocal } from './estado-local'


/** Un servidor remoto recordado (`cliente_servidores` + `cl_cliente_servidor_extra`). */
export interface ServidorGuardado {
  id: string
  host: string
  puerto: number
  https: boolean
  tipoCredencial: TipoCredencial
  apiKey: string | null
  ultimoUsuario: string | null
  /** Token de sesión persistente (30 días); la contraseña NUNCA se guarda. */
  tokenSesion: string | null
  /** Epoch en segundos. */
  expiraSesion: number | null
  confiarEnCertificado: boolean
  nombre: string | null
  serverUuid: string | null
  huella: string | null
  certPem: string | null
}

export type DatosServidor = Partial<Omit<ServidorGuardado, 'id' | 'host' | 'puerto' | 'https'>> & { host: string; puerto: number; https: boolean }

interface Fila {
  id: string
  host: string
  puerto: number
  https: number
  tipo_credencial: string | null
  api_key: string | null
  ultimo_usuario_sesion: string | null
  sesion_token_persistente: string | null
  expira_sesion_persistente: number | null
  confiar_en_certificado: number
  nombre_servidor: string | null
  server_uuid: string | null
  huella_cert_sha256: string | null
  cert_pem: string | null
}

const SELECT = `SELECT s.*, e.nombre_servidor, e.server_uuid, e.huella_cert_sha256, e.cert_pem
  FROM cliente_servidores s LEFT JOIN cl_cliente_servidor_extra e ON e.servidor_id = s.id`

const aServidor = (f: Fila): ServidorGuardado => ({
  id: f.id,
  host: f.host,
  puerto: f.puerto,
  https: f.https === 1,
  // NULL = apiKey (como en Flutter)
  tipoCredencial: f.tipo_credencial === 'sesion' ? 'sesion' : 'apiKey',
  apiKey: f.api_key,
  ultimoUsuario: f.ultimo_usuario_sesion,
  tokenSesion: f.sesion_token_persistente,
  expiraSesion: f.expira_sesion_persistente,
  confiarEnCertificado: f.confiar_en_certificado === 1,
  nombre: f.nombre_servidor,
  serverUuid: f.server_uuid,
  huella: f.huella_cert_sha256,
  certPem: f.cert_pem
})

/** Servidores remotos recordados en `cliente_datos.sqlite`. */
export class ServidoresRepo {
  constructor(private readonly db: Database.Database) {}

  /** Progreso, «Próximas a ver» y colecciones de ESTE equipo para ese servidor (Modo Cliente con token). */
  estadoLocal(servidorId: string): EstadoLocal {
    return new EstadoLocal(this.db, servidorId)
  }

  listar(): ServidorGuardado[] {
    return (this.db.prepare(`${SELECT} ORDER BY s.ultima_conexion DESC`).all() as Fila[]).map(aServidor)
  }

  obtener(id: string): ServidorGuardado | null {
    const f = this.db.prepare(`${SELECT} WHERE s.id = ?`).get(id) as Fila | undefined
    return f ? aServidor(f) : null
  }

  porDireccion(host: string, puerto: number): ServidorGuardado | null {
    const f = this.db.prepare(`${SELECT} WHERE s.host = ? AND s.puerto = ?`).get(host, puerto) as Fila | undefined
    return f ? aServidor(f) : null
  }

  /** Crea o actualiza (por host+puerto, que es único). Lo que no se pasa se conserva; `null` lo borra. */
  guardar(d: DatosServidor, ahora = Math.floor(Date.now() / 1000)): ServidorGuardado {
    const previo = this.porDireccion(d.host, d.puerto)
    const id = previo?.id ?? randomUUID()
    const m = { ...(previo ?? {}), ...d }
    this.db.transaction(() => {
      this.db
        .prepare(
          `INSERT INTO cliente_servidores (id, host, puerto, ultima_conexion, api_key, tipo_credencial, ultimo_usuario_sesion, https, confiar_en_certificado, sesion_token_persistente, expira_sesion_persistente)
           VALUES (@id, @host, @puerto, @ahora, @api_key, @tipo, @usuario, @https, @confiar, @token, @expira)
           ON CONFLICT(id) DO UPDATE SET ultima_conexion = @ahora, api_key = @api_key, tipo_credencial = @tipo, ultimo_usuario_sesion = @usuario, https = @https,
             confiar_en_certificado = @confiar, sesion_token_persistente = @token, expira_sesion_persistente = @expira`
        )
        .run({
          id,
          host: d.host,
          puerto: d.puerto,
          ahora,
          api_key: m.apiKey ?? null,
          tipo: m.tipoCredencial ?? 'apiKey',
          usuario: m.ultimoUsuario ?? null,
          https: d.https ? 1 : 0,
          confiar: m.confiarEnCertificado ? 1 : 0,
          token: m.tokenSesion ?? null,
          expira: m.expiraSesion ?? null
        })
      this.db
        .prepare(
          `INSERT INTO cl_cliente_servidor_extra (servidor_id, nombre_servidor, server_uuid, huella_cert_sha256, cert_pem) VALUES (@id, @nombre, @uuid, @huella, @pem)
           ON CONFLICT(servidor_id) DO UPDATE SET nombre_servidor = @nombre, server_uuid = @uuid, huella_cert_sha256 = @huella, cert_pem = @pem`
        )
        .run({ id, nombre: m.nombre ?? null, uuid: m.serverUuid ?? null, huella: m.huella ?? null, pem: m.certPem ?? null })
    })()
    return this.obtener(id)!
  }

  /** Quita el servidor y, en cascada, su progreso/colecciones locales. */
  eliminar(id: string): void {
    this.db.prepare('DELETE FROM cliente_servidores WHERE id = ?').run(id)
  }

  /** Cierra la sesión persistente (el token vencido o revocado ya no sirve). La contraseña nunca estuvo guardada. */
  olvidarSesion(id: string): void {
    this.db.prepare('UPDATE cliente_servidores SET sesion_token_persistente = NULL, expira_sesion_persistente = NULL WHERE id = ?').run(id)
  }
}
