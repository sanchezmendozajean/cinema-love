import { randomBytes } from 'node:crypto'
import type { AddressInfo } from 'node:net'
import type { FastifyInstance } from 'fastify'
import { crearApp, type ServerDeps } from './app'

export interface ListenerInterno {
  baseUrl: string
  /** Token admin efímero: se genera por sesión, nunca se persiste ni sale de la máquina. */
  token: string
  cerrar(): Promise<void>
}

/** Levanta la API en 127.0.0.1 con puerto efímero y token admin (specs/01 §2). */
export async function iniciarListenerInterno(deps: Omit<ServerDeps, 'adminToken'>): Promise<ListenerInterno> {
  const token = randomBytes(24).toString('hex')
  const app: FastifyInstance = await crearApp({ ...deps, adminToken: token })
  await app.listen({ host: '127.0.0.1', port: 0 })
  const { port } = app.server.address() as AddressInfo
  return { baseUrl: `http://127.0.0.1:${port}`, token, cerrar: () => app.close() }
}
