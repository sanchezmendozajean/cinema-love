import { timingSafeEqual } from 'node:crypto'
import type { FastifyReply, FastifyRequest } from 'fastify'
import type { Capability, TipoPrincipal } from '../../shared/api-types'
import type { AuthRepo } from '../repositories/auth'

/** Quién hace la petición (specs/04 §2). */
export interface Principal {
  tipo: TipoPrincipal
  /** Fila de progreso/colecciones: null = admin local (también para la API key, como Flutter). */
  usuarioId: string | null
  nombreUsuario?: string
  /** Token de sesión usado, para `POST /logout`. */
  token?: string
}

declare module 'fastify' {
  interface FastifyRequest {
    principal: Principal | null
  }
}

export const RUTAS_PUBLICAS = new Set(['/api/v1/health', '/api/v1/login'])

const iguales = (a: string, b: string): boolean => {
  const x = Buffer.from(a)
  const y = Buffer.from(b)
  return x.length === y.length && timingSafeEqual(x, y)
}

/** `X-Api-Key` o `?api_key=` (necesario para `<video src>` y `<img src>`). */
export function credencialDe(req: FastifyRequest): string | null {
  const h = req.headers['x-api-key']
  if (typeof h === 'string' && h) return h
  const q = (req.query as Record<string, unknown> | undefined)?.api_key
  return typeof q === 'string' && q ? q : null
}

export function resolverPrincipal(credencial: string, auth: AuthRepo, adminToken: string | null): Principal | null {
  if (adminToken && iguales(credencial, adminToken)) return { tipo: 'admin', usuarioId: null }
  const key = auth.apiKeyActiva()
  if (key && iguales(credencial, key)) return { tipo: 'token', usuarioId: null }
  const u = auth.usuarioDeToken(credencial)
  if (u) return { tipo: 'usuario', usuarioId: u.id, nombreUsuario: u.nombreUsuario, token: credencial }
  return null
}

export function capabilitiesDe(tipo: TipoPrincipal): Capability[] {
  switch (tipo) {
    case 'admin':
      return ['browse', 'play', 'progress', 'userLists', 'admin']
    case 'usuario':
      return ['browse', 'play', 'progress', 'userLists']
    case 'token':
      return ['browse', 'play', 'progress']
  }
}

export const error = (reply: FastifyReply, status: number, mensaje: string): FastifyReply =>
  reply.code(status).send({ error: mensaje })
