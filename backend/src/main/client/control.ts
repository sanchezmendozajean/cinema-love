import { randomBytes } from 'node:crypto'
import type { AddressInfo } from 'node:net'
import Fastify from 'fastify'
import type { AppCore } from '../app-core'
import type { PeticionConexion } from '../../shared/cliente-types'
import { Espejo } from './espejo'

/**
 * Servidor de control del sidecar en Modo Cliente (versión Qt): sin colección local, la app Qt gestiona desde aquí la conexión con otro
 * CinemaLove (verificar, conectar, reconectar, olvidar) y el espejo del catálogo. Solo escucha en 127.0.0.1 y exige un token efímero.
 * Una vez conectado, el resto de la API (progreso, listas, imágenes, streams) se pide al proxy local (`estado.api`), no aquí.
 */
export async function iniciarControl(core: AppCore, carpeta: string): Promise<{ baseUrl: string; token: string; cerrar(): Promise<void> }> {
  const token = randomBytes(24).toString('hex')
  const app = Fastify({ logger: false, forceCloseConnections: true })
  let espejo: Espejo | null = null

  app.setErrorHandler((err, _req, reply) => { console.error('control:', err.stack ?? err); void reply.code(500).send({ error: err.message }) })
  app.addHook('onRequest', async (req, reply) => {
    if (req.headers['x-api-key'] !== token) return reply.code(401).send({ error: 'No autorizado.' })
  })

  /** Lanza (o relanza) la construcción del espejo cuando hay una conexión activa. */
  const lanzarEspejo = (): void => {
    espejo?.cancelar()
    espejo = null
    const e = core.estadoConexion
    if (e.tipo !== 'conectada') return
    espejo = new Espejo(carpeta, e.api, e.info.servidorId ?? 'servidor')
    void espejo.construir()
  }

  app.get('/control/estado', async () => ({
    conexion: core.estadoConexion,
    espejo: espejo?.estado ?? { fase: 'inactivo', hecho: 0, total: 0, ruta: null, mensaje: '' },
    recordados: core.servidoresRecordados()
  }))
  app.post<{ Body: { url: string } }>('/control/probar', async (req) => core.probarServidor(String(req.body?.url ?? '')))
  app.post<{ Body: PeticionConexion }>('/control/conectar', async (req) => {
    const r = await core.conectar(req.body, false)
    if (r.ok) lanzarEspejo()
    return r
  })
  app.post<{ Body: { id: string } }>('/control/reconectar', async (req) => {
    try {
      const estado = await core.conexionCliente().reconectar(String(req.body?.id))
      lanzarEspejo()
      return { ok: true, estado }
    } catch (e) {
      return { ok: false, codigo: 'sin-conexion', mensaje: e instanceof Error ? e.message : String(e) }
    }
  })
  app.post<{ Body: { password: string } }>('/control/renovar', async (req) => {
    const r = await core.renovarSesion(String(req.body?.password ?? ''))
    if (r.ok) lanzarEspejo()
    return r
  })
  app.post<{ Body: { huella: string } }>('/control/confiar', async (req) => {
    const r = await core.confiarCertificadoNuevo(String(req.body?.huella ?? ''))
    if (r.ok) lanzarEspejo()
    return r
  })
  app.post('/control/reintentar', async () => {
    const estado = await core.reintentarConexion()
    lanzarEspejo()
    return { ok: true, estado }
  })
  app.post('/control/personal', async () => {
    await espejo?.actualizarPersonal()
    return { ok: true }
  })
  app.post('/control/refrescar', async () => { lanzarEspejo(); return { ok: true } })
  app.post('/control/desconectar', async () => {
    espejo?.cancelar()
    espejo = null
    await core.desconectar()
    return { ok: true }
  })
  app.post('/control/olvidar', async () => {
    espejo?.cancelar()
    espejo = null
    await core.olvidarServidor()
    return { ok: true }
  })

  await app.listen({ host: '127.0.0.1', port: 0 })
  const puerto = (app.server.address() as AddressInfo).port
  return {
    baseUrl: `http://127.0.0.1:${puerto}`,
    token,
    cerrar: async () => { espejo?.cancelar(); await app.close() }
  }
}
