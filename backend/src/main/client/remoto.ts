import http from 'node:http'
import https from 'node:https'
import tls from 'node:tls'
import { normalizarHuella } from '../server/certs'

/** A dónde se conecta el cliente. Con `certPem`, el certificado del servidor se exige IGUAL a ese (autofirmado confiado). */
export interface Destino {
  host: string
  puerto: number
  https: boolean
  certPem?: string | null
}

export const urlDe = (d: Pick<Destino, 'host' | 'puerto' | 'https'>): string => `${d.https ? 'https' : 'http'}://${d.host.includes(':') ? `[${d.host}]` : d.host}:${d.puerto}`

/** Acepta `http://192.168.0.115:8080`, `https://sala.local` o `192.168.0.115:8080` (sin esquema = HTTP). */
export function parsearUrl(texto: string): Destino | null {
  const t = texto.trim()
  if (!t) return null
  try {
    const u = new URL(/^[a-z][a-z0-9+.-]*:\/\//i.test(t) ? t : `http://${t}`)
    if (u.protocol !== 'http:' && u.protocol !== 'https:') return null
    if (!u.hostname) return null
    const esHttps = u.protocol === 'https:'
    return { host: u.hostname.replace(/^\[|\]$/g, ''), puerto: u.port ? Number(u.port) : esHttps ? 443 : 80, https: esHttps }
  } catch {
    return null
  }
}

/** PEM de un certificado a partir de su DER. */
export function derAPem(der: Buffer): string {
  const b64 = der.toString('base64').match(/.{1,64}/g)!.join('\n')
  return `-----BEGIN CERTIFICATE-----\n${b64}\n-----END CERTIFICATE-----\n`
}

export interface CertificadoRemoto {
  pem: string
  huella: string
  /** `true` si el sistema ya lo considera válido (firmado por una CA conocida): no hace falta pedir confianza. */
  valido: boolean
}

/** Lee el certificado que presenta el servidor (sin confiar en él todavía). */
export function leerCertificado(host: string, puerto: number, timeoutMs = 5000): Promise<CertificadoRemoto> {
  return new Promise((resolver, rechazar) => {
    const s = tls.connect({ host, port: puerto, servername: /^[\d.]+$/.test(host) || host.includes(':') ? undefined : host, rejectUnauthorized: false, timeout: timeoutMs })
    s.once('secureConnect', () => {
      const c = s.getPeerCertificate()
      const salida = { pem: derAPem(c.raw), huella: c.fingerprint256, valido: s.authorized }
      s.end()
      resolver(salida)
    })
    s.once('timeout', () => {
      s.destroy()
      rechazar(new Error('tiempo agotado'))
    })
    s.once('error', rechazar)
  })
}

export interface OpcionesPeticion {
  metodo?: string
  /** Ruta con query: `/api/v1/health`. */
  ruta: string
  headers?: http.OutgoingHttpHeaders
  cuerpo?: Buffer | string
  timeoutMs?: number
  senal?: AbortSignal
}

/** Petición al servidor remoto devolviendo la respuesta SIN consumir (el llamador la lee o la transmite). */
export async function pedir(d: Destino, o: OpcionesPeticion): Promise<http.IncomingMessage> {
  try {
    return await pedirUnaVez(d, o)
  } catch (e) {
    // Si el servidor se reinició, el socket keep-alive que Node guardó ya está muerto: la primera petición lo descubre con
    // ECONNRESET sin que el servidor la haya procesado. Una lectura (GET/HEAD) se repite una vez con una conexión nueva.
    const metodo = (o.metodo ?? 'GET').toUpperCase()
    const codigo = (e as NodeJS.ErrnoException).code
    if ((metodo === 'GET' || metodo === 'HEAD') && (codigo === 'ECONNRESET' || codigo === 'EPIPE') && !o.senal?.aborted) return pedirUnaVez(d, o)
    throw e
  }
}

function pedirUnaVez(d: Destino, o: OpcionesPeticion): Promise<http.IncomingMessage> {
  return new Promise((resolver, rechazar) => {
    const comunes = { host: d.host, port: d.puerto, method: o.metodo ?? 'GET', path: o.ruta, headers: o.headers, timeout: o.timeoutMs ?? 10_000, signal: o.senal }
    const req = d.https
      ? https.request({
          ...comunes,
          ...(d.certPem
            ? // Certificado confiado: la cadena se valida contra ESE certificado exacto (antes de enviar nada) y solo se omite el
              // nombre de host, porque el autofirmado se emite para una IP que puede cambiar.
              { ca: [d.certPem], rejectUnauthorized: true, checkServerIdentity: () => undefined }
            : { rejectUnauthorized: true })
        })
      : http.request(comunes)
    req.once('response', resolver)
    req.once('timeout', () => req.destroy(new Error('tiempo agotado')))
    req.once('error', rechazar)
    req.end(o.cuerpo)
  })
}

async function leer(res: http.IncomingMessage, maxBytes = 1_000_000): Promise<string> {
  const trozos: Buffer[] = []
  let total = 0
  for await (const t of res) {
    total += (t as Buffer).length
    if (total > maxBytes) {
      res.destroy()
      throw new Error('respuesta demasiado grande')
    }
    trozos.push(t as Buffer)
  }
  return Buffer.concat(trozos).toString('utf8')
}

/** GET/POST JSON al servidor remoto. */
export async function pedirJson<T>(d: Destino, o: OpcionesPeticion & { json?: unknown }): Promise<{ status: number; cuerpo: T | null }> {
  const cuerpo = o.json === undefined ? o.cuerpo : JSON.stringify(o.json)
  const headers = { ...(o.json === undefined ? {} : { 'Content-Type': 'application/json' }), ...o.headers }
  const res = await pedir(d, { ...o, headers, cuerpo })
  const texto = await leer(res)
  let parseado: T | null = null
  try {
    parseado = JSON.parse(texto) as T
  } catch {
    /* no era JSON */
  }
  return { status: res.statusCode ?? 0, cuerpo: parseado }
}

export interface SaludRemota {
  status: string
  app?: string
  serverId?: string
  nombre?: string
  apiVersion?: number
  plataforma?: string
}

export type ResultadoSondeo =
  | { tipo: 'ok'; destino: Destino; salud: SaludRemota }
  /** HTTPS con un certificado que el sistema no conoce: hay que mostrar la huella y pedir «Confío en este certificado». */
  | { tipo: 'certificado-no-confiable'; destino: Destino; huella: string; pem: string; salud: SaludRemota | null }
  | { tipo: 'certificado-cambiado'; destino: Destino; huella: string; pem: string }
  | { tipo: 'sin-respuesta' }
  | { tipo: 'no-cinemalove' }

const CODIGOS_CERTIFICADO = new Set(['DEPTH_ZERO_SELF_SIGNED_CERT', 'SELF_SIGNED_CERT_IN_CHAIN', 'UNABLE_TO_VERIFY_LEAF_SIGNATURE', 'CERT_HAS_EXPIRED', 'ERR_TLS_CERT_ALTNAME_INVALID', 'UNABLE_TO_GET_ISSUER_CERT_LOCALLY', 'CERT_UNTRUSTED'])

const esSaludCinemaLove = (s: SaludRemota | null): s is SaludRemota => !!s && s.status === 'ok' && s.app === 'cinemalove'

/**
 * Comprueba una dirección con `GET /api/v1/health` sin credencial (specs/03 §2). Distingue: sin respuesta, algo que no es
 * un servidor CinemaLove, y HTTPS con un certificado nuevo o cambiado (que pide confianza explícita).
 */
export async function sondear(destino: Destino): Promise<ResultadoSondeo> {
  try {
    const { status, cuerpo } = await pedirJson<SaludRemota>(destino, { ruta: '/api/v1/health', timeoutMs: 5000 })
    return status === 200 && esSaludCinemaLove(cuerpo) ? { tipo: 'ok', destino, salud: cuerpo } : { tipo: 'no-cinemalove' }
  } catch (e) {
    const codigo = (e as NodeJS.ErrnoException).code ?? ''
    if (!destino.https || !CODIGOS_CERTIFICADO.has(codigo)) return { tipo: 'sin-respuesta' }
    // Certificado que no se puede validar: se lee para mostrar su huella (no se confía todavía).
    try {
      const c = await leerCertificado(destino.host, destino.puerto)
      if (destino.certPem) return { tipo: 'certificado-cambiado', destino, huella: c.huella, pem: c.pem }
      // Con ese certificado y solo para esta lectura, se identifica al servidor (nombre) para mostrarlo en el aviso.
      let salud: SaludRemota | null = null
      try {
        const r = await pedirJson<SaludRemota>({ ...destino, certPem: c.pem }, { ruta: '/api/v1/health', timeoutMs: 5000 })
        salud = r.status === 200 && esSaludCinemaLove(r.cuerpo) ? r.cuerpo : null
      } catch {
        salud = null
      }
      if (!salud) return { tipo: 'no-cinemalove' }
      return { tipo: 'certificado-no-confiable', destino, huella: c.huella, pem: c.pem, salud }
    } catch {
      return { tipo: 'sin-respuesta' }
    }
  }
}

/** ¿Esta huella (escrita de cualquier forma) es la del certificado `pem`? */
export const huellaCoincide = (a: string, b: string): boolean => normalizarHuella(a) === normalizarHuella(b)
