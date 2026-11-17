import bcrypt from 'bcryptjs'
import type { FastifyInstance } from 'fastify'
import type { Health, LoginRespuesta, Me } from '../../../shared/api-types'
import { capabilitiesDe, error } from '../auth'
import type { Contexto } from '../app'

const MAX_INTENTOS = 5
const VENTANA_MS = 60_000

export function rutasSistema(app: FastifyInstance, ctx: Contexto): void {
  const intentos = new Map<string, number[]>()
  /** Rate limit suave de /login: 5 intentos por minuto por IP. */
  const excedido = (ip: string): boolean => {
    const ahora = Date.now()
    const recientes = (intentos.get(ip) ?? []).filter((t) => ahora - t < VENTANA_MS)
    recientes.push(ahora)
    intentos.set(ip, recientes)
    return recientes.length > MAX_INTENTOS
  }

  app.get('/api/v1/health', async (): Promise<Health> => ({
    status: 'ok',
    app: 'cinemalove',
    serverId: ctx.auth.serverId(),
    nombre: ctx.nombreServidor(),
    apiVersion: 1,
    plataforma: 'electron'
  }))

  app.post('/api/v1/login', async (req, reply) => {
    if (excedido(req.ip)) return error(reply, 429, 'Demasiados intentos. Espera un minuto e inténtalo de nuevo.')
    const body = req.body as { nombreUsuario?: unknown; password?: unknown } | null
    if (typeof body?.nombreUsuario !== 'string' || typeof body.password !== 'string') {
      return error(reply, 400, 'nombreUsuario y password son requeridos.')
    }
    const usuario = ctx.auth.usuarioPorNombre(body.nombreUsuario)
    // Se compara siempre (hash de relleno si el usuario no existe) para no revelar por tiempo qué usuarios hay.
    const hash = usuario?.passwordHash ?? '$2b$10$invalidinvalidinvalidinvalidinvalidinvalidinvalidinvali'
    const ok = await bcrypt.compare(body.password, hash).catch(() => false)
    if (!usuario || !ok) return error(reply, 401, 'Usuario o contraseña inválidos.')
    const respuesta: LoginRespuesta = {
      token: ctx.auth.crearSesion(usuario.id),
      usuarioId: usuario.id,
      nombreUsuario: usuario.nombreUsuario
    }
    return respuesta
  })

  app.post('/api/v1/logout', async (req) => {
    if (req.principal?.token) ctx.auth.invalidarSesion(req.principal.token)
    return { ok: true }
  })

  app.get('/api/v1/me', async (req): Promise<Me> => {
    const p = req.principal!
    return {
      principal: p.tipo,
      ...(p.nombreUsuario ? { nombreUsuario: p.nombreUsuario } : {}),
      capabilities: capabilitiesDe(p.tipo)
    }
  })
}
