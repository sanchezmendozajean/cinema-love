import { createReadStream, statSync } from 'node:fs'
import type { FastifyReply, FastifyRequest } from 'fastify'

export const MIME_VIDEO: Record<string, string> = {
  '.mp4': 'video/mp4',
  '.m4v': 'video/x-m4v',
  '.mkv': 'video/x-matroska',
  '.avi': 'video/x-msvideo',
  '.mov': 'video/quicktime',
  '.wmv': 'video/x-ms-wmv',
  '.flv': 'video/x-flv',
  '.webm': 'video/webm',
  '.ts': 'video/mp2t',
  '.mpg': 'video/mpeg',
  '.mpeg': 'video/mpeg'
}

/** Interpreta `Range: bytes=a-b` (un solo rango). null = sin rango; 'invalido' = no satisfacible. */
export function parsearRango(header: string | undefined, tamano: number): { ini: number; fin: number } | null | 'invalido' {
  if (!header) return null
  const m = /^bytes=(\d*)-(\d*)$/.exec(header.trim())
  if (!m || (m[1] === '' && m[2] === '')) return null
  let ini: number
  let fin: number
  if (m[1] === '') {
    const n = Number(m[2])
    if (n === 0) return 'invalido'
    ini = Math.max(tamano - n, 0)
    fin = tamano - 1
  } else {
    ini = Number(m[1])
    fin = m[2] === '' ? tamano - 1 : Math.min(Number(m[2]), tamano - 1)
  }
  return ini >= tamano || ini > fin ? 'invalido' : { ini, fin }
}

/** Sirve un archivo con soporte `Range` (206, `Content-Range`, `Accept-Ranges`). */
export function servirArchivo(req: FastifyRequest, reply: FastifyReply, ruta: string, contentType: string): FastifyReply {
  const { size } = statSync(ruta)
  const rango = parsearRango(req.headers.range, size)
  reply.header('Accept-Ranges', 'bytes').header('Content-Type', contentType)
  if (rango === 'invalido') return reply.code(416).header('Content-Range', `bytes */${size}`).send()
  if (!rango) return reply.header('Content-Length', size).send(createReadStream(ruta))
  return reply
    .code(206)
    .header('Content-Range', `bytes ${rango.ini}-${rango.fin}/${size}`)
    .header('Content-Length', rango.fin - rango.ini + 1)
    .send(createReadStream(ruta, { start: rango.ini, end: rango.fin }))
}
