import type { FastifyInstance, FastifyReply, FastifyRequest } from 'fastify'
import QRCode from 'qrcode'
import { ErrorAdmin, cambiarPassword, crearUsuario, eliminarUsuario, listarUsuarios, regenerarApiKey } from '../../services/admin-service'
import type { Contexto } from '../app'
import type { ConfigServidor } from '../servidor-publico'

type Envolver = <T extends FastifyRequest>(fn: (req: T, reply: FastifyReply) => Promise<unknown> | unknown) => (req: T, reply: FastifyReply) => Promise<unknown>

/**
 * Administración del servidor público, usuarios, token de acceso y sesiones activas (specs/03 §4). Se registra dentro de
 * `rutasAdmin`, así que hereda su guarda: solo el listener interno y solo el principal admin.
 */
export function rutasAdminServidor(app: FastifyInstance, ctx: Contexto, envolver: Envolver): void {
  const servidor = (): NonNullable<Contexto['servidor']> => {
    if (!ctx.servidor) throw new ErrorAdmin('El servidor no está disponible en esta instalación.', 503)
    return ctx.servidor
  }

  // ------------------------------------------------------------- servidor
  const vistaServidor = (): { config: ConfigServidor; estado: ReturnType<ReturnType<typeof servidor>['estado']> } => ({ config: servidor().config(), estado: servidor().estado() })

  app.get('/api/v1/admin/servidor', envolver(async () => vistaServidor()))

  app.put('/api/v1/admin/servidor/config', envolver(async (req) => {
    await servidor().guardarConfig((req.body ?? {}) as Partial<ConfigServidor>)
    return vistaServidor()
  }))

  app.post('/api/v1/admin/servidor/iniciar', envolver(async () => {
    await servidor().iniciar()
    return vistaServidor()
  }))

  app.post('/api/v1/admin/servidor/detener', envolver(async () => {
    await servidor().detener()
    return vistaServidor()
  }))

  /** Los clientes tendrán que volver a confiar en el certificado: la interfaz lo confirma antes de llamar. */
  app.post('/api/v1/admin/servidor/certificado/regenerar', envolver(async () => {
    await servidor().regenerarCertificado()
    return vistaServidor()
  }))

  /** QR (SVG) de una dirección para conectarse; pensado para el futuro cliente Android. */
  app.get('/api/v1/admin/servidor/qr', envolver(async (req) => {
    const url = (req.query as { url?: string }).url ?? ''
    if (!/^https?:\/\/[^\s]+$/.test(url) || url.length > 200) throw new ErrorAdmin('Dirección inválida.')
    return { svg: await QRCode.toString(url, { type: 'svg', margin: 1, errorCorrectionLevel: 'M' }) }
  }))

  // ------------------------------------------------------------- sesiones activas
  app.get('/api/v1/admin/sesiones', envolver(async () => ({
    conversiones: ctx.reproduccion?.playback.listarSesiones() ?? [],
    directas: ctx.actividad.activas()
  })))

  app.delete('/api/v1/admin/sesiones/:id', envolver(async (req) => {
    const detenida = ctx.reproduccion?.playback.detener((req.params as { id: string }).id) ?? false
    if (!detenida) throw new ErrorAdmin('La sesión ya terminó.', 404)
    return { ok: true }
  }))

  // ------------------------------------------------------------- usuarios
  app.get('/api/v1/admin/usuarios', envolver(async () => listarUsuarios(ctx.db)))

  app.post('/api/v1/admin/usuarios', envolver(async (req, reply) => {
    const id = await crearUsuario(ctx.db, (req.body ?? {}) as { nombreUsuario?: unknown; password?: unknown })
    reply.code(201)
    return { id }
  }))

  app.put('/api/v1/admin/usuarios/:id/password', envolver(async (req) => {
    await cambiarPassword(ctx.db, (req.params as { id: string }).id, (req.body as { password?: unknown } | null)?.password)
    return { ok: true }
  }))

  app.delete('/api/v1/admin/usuarios/:id', envolver(async (req) => {
    eliminarUsuario(ctx.db, (req.params as { id: string }).id, (req.query as { confirm?: unknown }).confirm)
    return { ok: true }
  }))

  // ------------------------------------------------------------- token de acceso
  app.get('/api/v1/admin/api-key', envolver(async () => ({ valor: ctx.auth.apiKeyActiva() })))

  app.post('/api/v1/admin/api-key/regenerar', envolver(async () => ({ valor: regenerarApiKey(ctx.db) })))
}
