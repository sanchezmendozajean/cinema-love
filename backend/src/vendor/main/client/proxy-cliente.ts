import { randomBytes } from 'node:crypto'
import type { AddressInfo } from 'node:net'
import cors from '@fastify/cors'
import Fastify from 'fastify'
import type { LectorRemoto, Personal } from './personal'
import { pedir, pedirJson, type Destino } from './remoto'

/** Cabeceras que no se reenvían tal cual entre las dos conexiones. */
const SALTAR = new Set(['connection', 'keep-alive', 'transfer-encoding', 'proxy-authenticate', 'proxy-authorization', 'te', 'trailer', 'upgrade', 'host'])
const PASAR_AL_REMOTO = ['range', 'accept', 'content-type', 'user-agent', 'if-none-match', 'if-modified-since', 'accept-language']

export interface OpcionesProxy {
  destino: Destino
  /** Credencial del servidor remoto (token de sesión o API key). Nunca sale hacia el renderer. */
  credencial: () => string
  nombreServidor: string
  /** El servidor respondió 401 a una credencial que antes servía: la sesión venció o fue revocada. */
  alNoAutorizado?: () => void
  /** No hay forma de hablar con el servidor (apagado, red caída) o su certificado cambió. */
  alFallar?: (motivo: 'sin-conexion' | 'certificado-cambiado') => void
  /**
   * Con token, lo personal (progreso, Próximas a ver, colecciones) se atiende en este equipo: recibe cómo leer JSON del
   * servidor remoto y devuelve quien responde esas rutas.
   */
  personal?: (leerRemoto: LectorRemoto) => Personal
}

export interface ProxyActivo {
  baseUrl: string
  /** Token efímero del renderer para ESTE proxy (distinto de la credencial remota). */
  token: string
  cerrar(): Promise<void>
}

const CODIGOS_CERT = /CERT|SELF_SIGNED|UNABLE_TO_VERIFY|ERR_TLS|SSL/i

/**
 * Listener local (127.0.0.1) del Modo Cliente: el renderer sigue hablando con un solo origen local —igual que en Modo
 * Local—, y este proxy reenvía a `/api/v1/*` del servidor remoto. Así el CSP del renderer no cambia, el certificado
 * autofirmado se valida en Node contra el certificado confiado, y la credencial remota no pasa por el renderer.
 */
export async function crearProxyCliente(o: OpcionesProxy): Promise<ProxyActivo> {
  const token = randomBytes(24).toString('hex')
  const app = Fastify({ logger: false, forceCloseConnections: true })
  await app.register(cors, { origin: true, methods: ['GET', 'POST', 'PUT', 'DELETE'], allowedHeaders: ['X-Api-Key', 'Content-Type', 'Range'], exposedHeaders: ['Content-Range', 'Accept-Ranges'] })
  // El cuerpo se reenvía sin interpretarlo.
  app.removeAllContentTypeParsers()
  app.addContentTypeParser('*', { parseAs: 'buffer' }, (_req, body, hecho) => hecho(null, body))

  /** Falló la conversación con el servidor: se avisa (para confirmar si cayó o cambió su certificado) y se responde 502. */
  const respuestaDeFallo = (e: unknown): { status: number; cuerpo: { error: string; codigo: string } } => {
    const cert = CODIGOS_CERT.test(String((e as NodeJS.ErrnoException).code ?? '') + String((e as Error).message))
    o.alFallar?.(cert && o.destino.https ? 'certificado-cambiado' : 'sin-conexion')
    return {
      status: 502,
      cuerpo: {
        error: cert && o.destino.https ? `El certificado de «${o.nombreServidor}» cambió. Vuelve a conectarte para confiar en el nuevo.` : `No pudimos conectar con «${o.nombreServidor}».`,
        codigo: cert ? 'certificado-cambiado' : 'sin-conexion'
      }
    }
  }

  const leerRemoto: LectorRemoto = async (ruta, cuerpo) => {
    try {
      const r = await pedirJson<unknown>(o.destino, { metodo: cuerpo === undefined ? 'GET' : 'POST', json: cuerpo, ruta: `/api/v1${ruta}`, headers: { 'x-api-key': o.credencial() }, timeoutMs: 20_000 })
      if (r.status === 401) o.alNoAutorizado?.()
      return r.status === 200 ? r.cuerpo : null
    } catch (e) {
      respuestaDeFallo(e)
      return null
    }
  }
  const personal = o.personal?.(leerRemoto) ?? null

  app.route({
    method: ['GET', 'POST', 'PUT', 'DELETE'],
    url: '/api/v1/*',
    handler: async (req, reply) => {
      const url = new URL(req.url, 'http://local')
      const publica = url.pathname === '/api/v1/health'
      const recibida = (req.headers['x-api-key'] as string | undefined) ?? url.searchParams.get('api_key')
      if (!publica && recibida !== token) return reply.code(401).send({ error: 'Credencial inválida o faltante.' })
      // La administración de un servidor ajeno no se maneja desde aquí (y el servidor la rechazaría de todos modos).
      if (url.pathname.startsWith('/api/v1/admin/')) return reply.code(404).send({ error: 'Ruta no encontrada.' })

      url.searchParams.delete('api_key')
      const headers: Record<string, string> = {}
      for (const h of PASAR_AL_REMOTO) if (typeof req.headers[h] === 'string') headers[h] = req.headers[h] as string
      if (!publica) headers['x-api-key'] = o.credencial()

      // Con token, lo personal se resuelve en este equipo (o se corrige encima de la respuesta del servidor).
      const ruta = url.pathname.slice('/api/v1'.length)
      const clase = personal?.clasificar(req.method, ruta) ?? null
      if (personal && clase === 'local') {
        let cuerpo: unknown = null
        if (Buffer.isBuffer(req.body) && req.body.length) {
          try {
            cuerpo = JSON.parse(req.body.toString('utf8'))
          } catch {
            return reply.code(400).send({ error: 'El cuerpo no es JSON válido.' })
          }
        }
        const r = await personal.manejar({ metodo: req.method, ruta, query: url.searchParams, cuerpo })
        return reply.code(r.status).send(r.cuerpo)
      }
      if (personal && clase === 'consulta') {
        const r = await personal.consultar(ruta, url.searchParams)
        return reply.code(r.status).send(r.cuerpo)
      }
      if (personal && clase === 'superponer') {
        try {
          const r = await pedirJson<unknown>(o.destino, { ruta: url.pathname + url.search, headers, timeoutMs: 30_000 })
          if (r.status === 401) reply.raw.once('finish', () => o.alNoAutorizado?.())
          if (r.status !== 200 || r.cuerpo === null) return reply.code(r.status || 502).send(r.cuerpo ?? { error: 'Respuesta inválida del servidor.' })
          return reply.send(await personal.superponer(r.cuerpo))
        } catch (e) {
          const f = respuestaDeFallo(e)
          return reply.code(f.status).send(f.cuerpo)
        }
      }

      const abortar = new AbortController()
      reply.raw.on('close', () => abortar.abort())
      let res
      try {
        res = await pedir(o.destino, { metodo: req.method, ruta: url.pathname + url.search, headers, cuerpo: Buffer.isBuffer(req.body) && req.body.length ? req.body : undefined, timeoutMs: 30_000, senal: abortar.signal })
      } catch (e) {
        if (abortar.signal.aborted) return reply // el renderer cerró la conexión
        const f = respuestaDeFallo(e)
        return reply.code(f.status).send(f.cuerpo)
      }
      // Se avisa cuando la respuesta 401 ya salió hacia el renderer: avisar antes cerraría el proxy a mitad de la entrega.
      if (res.statusCode === 401 && !publica) reply.raw.once('finish', () => o.alNoAutorizado?.())

      const salida: Record<string, string | string[]> = {}
      for (const [k, v] of Object.entries(res.headers)) {
        if (v === undefined || SALTAR.has(k) || k.startsWith('access-control-')) continue
        salida[k] = v
      }
      // `hijack` porque el cuerpo (a veces un video de gigas) se transmite sin pasar por la serialización de Fastify.
      reply.hijack()
      // CORS lo pone @fastify/cors en `onRequest`; al secuestrar la respuesta hay que conservar sus cabeceras.
      const cabecerasCors = reply.getHeaders()
      reply.raw.writeHead(res.statusCode ?? 502, { ...cabecerasCors, ...salida } as Record<string, string | string[] | number>)
      res.on('error', () => reply.raw.destroy())
      res.pipe(reply.raw)
      return reply
    }
  })

  app.setNotFoundHandler((_req, reply) => reply.code(404).send({ error: 'Ruta no encontrada.' }))
  await app.listen({ host: '127.0.0.1', port: 0 })
  const { port } = app.server.address() as AddressInfo
  return { baseUrl: `http://127.0.0.1:${port}`, token, cerrar: () => app.close() }
}
