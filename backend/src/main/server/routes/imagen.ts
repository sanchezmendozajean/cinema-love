import { createReadStream } from 'node:fs'
import type { FastifyInstance } from 'fastify'
import { error } from '../auth'
import type { Contexto } from '../app'
import { esUrlDeTmdb } from '../../services/image-service'

export function rutasImagen(app: FastifyInstance, ctx: Contexto): void {
  app.get('/api/v1/image', async (req, reply) => {
    const { src, size } = req.query as { src?: string; size?: string }
    if (!src || !esUrlDeTmdb(src)) return error(reply, 400, 'Solo se aceptan imágenes de image.tmdb.org.')
    if (size !== undefined && !/^(w\d{2,4}|original)$/.test(size)) return error(reply, 400, 'Tamaño inválido.')
    const img = await ctx.images.obtener(src, size)
    if (!img) return error(reply, 404, 'Imagen no disponible.')
    return reply
      .header('Content-Type', img.contentType)
      .header('Cache-Control', 'public, max-age=31536000, immutable')
      .header('Access-Control-Allow-Origin', '*') // <img> desde cualquier origen; la credencial va en la query
      .send(createReadStream(img.ruta))
  })
}
