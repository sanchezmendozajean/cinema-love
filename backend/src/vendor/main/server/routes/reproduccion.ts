import { existsSync } from 'node:fs'
import { extname } from 'node:path'
import type { FastifyInstance, FastifyReply, FastifyRequest } from 'fastify'
import { CAPACIDADES_BASE, type CapacidadesCliente, type PistaAudio, type PlaybackInfo, type SesionRequest, type SesionRespuesta } from '../../../shared/playback-types'
import { decidirModo } from '../../services/playback-decision'
import { ErrorSesion } from '../../services/playback-service'
import { buscarSubtitulosExternos } from '../../services/subtitulos'
import { error } from '../auth'
import type { Contexto } from '../app'
import { MIME_VIDEO, servirArchivo } from '../range'
import { rutaSegura } from './catalogo'

type Tipo = 'item' | 'extra'
interface Objetivo {
  tipo: Tipo
  id: string
  ruta: string
}

/** `?caps=` = JSON en base64 con lo que el cliente sabe reproducir; lo que falte se completa con la base conservadora. */
export function leerCapacidades(param: unknown): CapacidadesCliente {
  if (typeof param !== 'string' || !param) return CAPACIDADES_BASE
  try {
    const j = JSON.parse(Buffer.from(param, 'base64').toString('utf8')) as Partial<CapacidadesCliente>
    const lista = (x: unknown, defecto: string[]): string[] => (Array.isArray(x) ? x.filter((v): v is string => typeof v === 'string').map((v) => v.toLowerCase()) : defecto)
    return {
      video: lista(j.video, CAPACIDADES_BASE.video),
      audio: lista(j.audio, CAPACIDADES_BASE.audio),
      contenedores: lista(j.contenedores, CAPACIDADES_BASE.contenedores)
    }
  } catch {
    return CAPACIDADES_BASE
  }
}

/** Tamaño total declarado en las reaperturas con `Range` del stream de remux/transcode (el real no se conoce). */
const TOTAL_FICTICIO = 1e11

/** Audios que MP4 lleva sin recodificar (y que además el cliente declaró). */
const AUDIO_EN_MP4 = new Set(['aac', 'mp3', 'opus', 'flac'])

/** El menor de los topes de altura que estén definidos (0 o negativo = sin tope). `undefined` si no hay ninguno. */
export function alturaMaxima(...topes: number[]): number | undefined {
  const validos = topes.filter((n) => Number.isFinite(n) && n > 0).map(Math.trunc)
  return validos.length ? Math.min(...validos) : undefined
}

export function rutasReproduccion(app: FastifyInstance, ctx: Contexto): void {
  const { repos } = ctx
  const rep = ctx.reproduccion

  const propietario = (req: FastifyRequest): string => `${req.principal!.tipo}:${req.principal!.usuarioId ?? ''}`
  /** Quién y desde dónde reproduce, para el panel «Reproduciendo ahora». */
  const quien = (req: FastifyRequest): string => (req.principal!.tipo === 'admin' ? 'Administrador (este equipo)' : req.principal!.tipo === 'token' ? 'Token de acceso' : (req.principal!.nombreUsuario ?? 'Usuario'))
  const clienteDe = (req: FastifyRequest): string => {
    const ua = String(req.headers['user-agent'] ?? '')
    const app = ua.includes('Electron/') ? 'CinemaLove' : /Dart/.test(ua) ? 'CinemaLove (Android)' : (ua.split(' ')[0] || 'Cliente').slice(0, 30)
    return `${app} · ${req.ip}`
  }
  const tituloDe = (tipo: Tipo, id: string): string => {
    const fila = ctx.db.prepare(tipo === 'item' ? 'SELECT titulo, ruta_relativa FROM media_items WHERE id = ?' : 'SELECT titulo, ruta_relativa FROM extras WHERE id = ?').get(id) as { titulo: string | null; ruta_relativa: string } | undefined
    return fila?.titulo ?? fila?.ruta_relativa ?? id
  }
  const sinFfmpeg = (reply: FastifyReply): FastifyReply =>
    error(reply, 503, 'La reproducción con conversión no está disponible: falta ffmpeg en esta instalación.')

  /** Resuelve el archivo físico de una película/episodio o de un extra, validando que esté dentro de su biblioteca. */
  function objetivo(tipo: Tipo, id: string, reply: FastifyReply): Objetivo | FastifyReply {
    const item = tipo === 'item' ? repos.obtenerItem(id) : null
    const extra = tipo === 'extra' ? repos.obtenerExtra(id) : null
    const padre = tipo === 'item' ? item : extra ? repos.obtenerItem(extra.mediaItemId) : null
    if (!padre || (tipo === 'extra' && !extra)) return error(reply, 404, tipo === 'item' ? 'Ítem no encontrado.' : 'Extra no encontrado.')
    const lib = repos.obtenerBiblioteca(padre.libraryId)
    if (!lib) return error(reply, 404, 'Biblioteca no encontrada.')
    const ruta = rutaSegura(lib, (item ?? extra)!.rutaRelativa)
    if (!ruta) return error(reply, 400, 'Ruta de archivo inválida.')
    if (!existsSync(ruta)) return error(reply, 404, 'El archivo no se encuentra en la ruta actual.')
    return { tipo, id, ruta }
  }
  const esRespuesta = (x: Objetivo | FastifyReply): x is FastifyReply => 'send' in x

  async function armarInfo(o: Objetivo, caps: CapacidadesCliente, audioElegido?: string): Promise<PlaybackInfo> {
    const probe = await rep!.probe.sondear(o.tipo, o.id, o.ruta)
    // Sin duración (sin TMDb o dato faltante) se guarda la de ffprobe: la ficha y «continuar viendo» la necesitan (specs/06 §3).
    if (o.tipo === 'item' && !ctx.soloLectura && probe.duracionSegundos > 0) {
      ctx.db.prepare('UPDATE media_items SET duracion_segundos = ? WHERE id = ? AND (duracion_segundos IS NULL OR duracion_segundos = 0)').run(Math.round(probe.duracionSegundos), o.id)
    }
    const externos = buscarSubtitulosExternos(o.ruta).map(({ archivo: _archivo, ...pista }) => pista)
    return {
      duracionSegundos: probe.duracionSegundos,
      contenedor: probe.contenedor,
      tamanoBytes: probe.tamanoBytes,
      video: probe.video,
      audio: probe.audio,
      subtitulos: [...probe.subtitulos, ...externos],
      decision: decidirModo(probe, caps, audioElegido)
    }
  }

  const manejarError = (reply: FastifyReply, e: unknown, que: string): FastifyReply => {
    if (e instanceof ErrorSesion) return error(reply, e.status, e.message)
    return error(reply, 500, `${que}: ${e instanceof Error ? e.message : 'error desconocido'}`)
  }

  for (const [prefijo, tipo] of [['media', 'item'], ['extras', 'extra']] as const) {
    // Archivo tal cual, con Range y el Content-Type correcto para <video>.
    app.get<{ Params: { id: string } }>(`/api/v1/${prefijo}/:id/direct`, async (req, reply) => {
      const o = objetivo(tipo, req.params.id, reply)
      if (esRespuesta(o)) return o
      ctx.actividad.marcar(`${propietario(req)}|${tipo}:${o.id}`, { quien: quien(req), titulo: tituloDe(tipo, o.id), cliente: clienteDe(req) })
      return servirArchivo(req, reply, o.ruta, MIME_VIDEO[extname(o.ruta).toLowerCase()] ?? 'application/octet-stream')
    })

    app.get<{ Params: { id: string }; Querystring: { caps?: string; audio?: string } }>(`/api/v1/${prefijo}/:id/playback-info`, async (req, reply) => {
      if (!rep) return sinFfmpeg(reply)
      const o = objetivo(tipo, req.params.id, reply)
      if (esRespuesta(o)) return o
      try {
        return await armarInfo(o, leerCapacidades(req.query.caps), req.query.audio)
      } catch (e) {
        return manejarError(reply, e, 'No se pudo analizar el archivo')
      }
    })

    // Subtítulo n (índice mpv de los internos, o `ext-k`) convertido a WebVTT.
    app.get<{ Params: { id: string; n: string } }>(`/api/v1/${prefijo}/:id/subtitles/:n`, async (req, reply) => {
      if (!rep) return sinFfmpeg(reply)
      const o = objetivo(tipo, req.params.id, reply)
      if (esRespuesta(o)) return o
      const idPista = req.params.n.replace(/\.vtt$/i, '')
      try {
        const probe = await rep.probe.sondear(o.tipo, o.id, o.ruta)
        const ext = buscarSubtitulosExternos(o.ruta).find((s) => s.id === idPista)
        const interno = probe.subtitulos.find((s) => s.id === idPista)
        const pista = ext ?? interno
        if (!pista) return error(reply, 404, 'Subtítulo no encontrado.')
        if (pista.tipo === 'imagen') return error(reply, 400, 'Este subtítulo es de imagen: solo puede quemarse en el video (requiere convertirlo).')
        const vtt = await rep.playback.subtituloAVtt({ ruta: o.ruta, indiceStream: interno?.indice ?? null, archivoExterno: ext?.archivo })
        return servirArchivo(req, reply, vtt, 'text/vtt; charset=utf-8').header('Cache-Control', 'private, max-age=3600')
      } catch (e) {
        return manejarError(reply, e, 'No se pudo convertir el subtítulo')
      }
    })
  }

  // --- sesiones de remux/transcode
  app.post('/api/v1/playback/sessions', async (req, reply) => {
    if (!rep) return sinFfmpeg(reply)
    const b = (req.body ?? {}) as SesionRequest & { caps?: unknown }
    const tipo: Tipo | null = typeof b.mediaItemId === 'string' ? 'item' : typeof b.extraId === 'string' ? 'extra' : null
    if (!tipo) return error(reply, 400, 'Falta mediaItemId o extraId.')
    const o = objetivo(tipo, (tipo === 'item' ? b.mediaItemId : b.extraId)!, reply)
    if (esRespuesta(o)) return o
    const inicio = typeof b.inicioSegundos === 'number' && Number.isFinite(b.inicioSegundos) ? Math.max(0, b.inicioSegundos) : 0
    const caps = leerCapacidades(typeof b.caps === 'string' ? b.caps : b.caps ? Buffer.from(JSON.stringify(b.caps)).toString('base64') : undefined)

    try {
      const probe = await rep.probe.sondear(o.tipo, o.id, o.ruta)
      const audio: PistaAudio | null = probe.audio.find((a) => a.id === b.audio) ?? probe.audio.find((a) => a.defecto) ?? probe.audio[0] ?? null
      const quemar = b.subtitulo ? probe.subtitulos.find((s) => s.id === b.subtitulo && s.tipo === 'imagen') ?? null : null
      let decision = decidirModo(probe, caps, audio?.id)
      const prefijo = tipo === 'item' ? 'media' : 'extras'

      if (b.modo === 'direct') return error(reply, 400, 'El modo direct no usa sesión: pide /direct.')
      if (b.modo) decision = { modo: b.modo, motivo: b.modo === 'transcode' ? 'Conversión pedida por el usuario' : 'Remux pedido' }
      // Cambiar de pista de audio en un archivo «direct» obliga a pasar por remux (el <video> no las alterna solo).
      const defecto = probe.audio.find((a) => a.defecto) ?? probe.audio[0]
      if (decision.modo === 'direct' && audio && defecto && audio.id !== defecto.id) decision = { modo: 'remux', motivo: 'Cambio de pista de audio' }
      // Subtítulo de imagen: solo se puede quemar en el video.
      if (quemar) decision = { modo: 'transcode', motivo: 'Subtítulo de imagen: hay que quemarlo en el video' }

      if (decision.modo === 'direct') {
        const directa: SesionRespuesta = { sessionId: '', url: `/api/v1/${prefijo}/${o.id}/direct`, modo: 'direct', offsetSegundos: 0, motivo: decision.motivo }
        return directa
      }

      return await rep.playback.crear({
        propietario: propietario(req),
        etiqueta: { titulo: tituloDe(tipo, o.id), quien: quien(req), cliente: clienteDe(req) },
        modo: decision.modo,
        ruta: o.ruta,
        inicioSegundos: inicio,
        video: probe.video,
        audio,
        subtituloQuemado: quemar,
        audioCompatible: !!audio && caps.audio.includes(audio.codec) && AUDIO_EN_MP4.has(audio.codec),
        // Tope de calidad al convertir: el menor entre lo que pide el cliente y lo que configuró quien administra el servidor.
        maxAltura: alturaMaxima(typeof b.maxAltura === 'number' ? b.maxAltura : 0, Number(ctx.auth.ajuste('cl.max_altura')) || 0),
        motivo: decision.motivo,
        bytesPorSegundo: probe.duracionSegundos > 0 ? probe.tamanoBytes / probe.duracionSegundos : undefined
      })
    } catch (e) {
      return manejarError(reply, e, 'No se pudo iniciar la reproducción')
    }
  })

  app.get<{ Params: { sid: string } }>('/api/v1/playback/sessions/:sid/stream', async (req, reply) => {
    if (!rep) return sinFfmpeg(reply)
    try {
      // Chromium reabre el stream con `Range: bytes=N-` cerca de donde reproduce (a veces sin que nadie salte). Se sirve desde
      // la ventana de la sesión; la sesión no se detiene al cerrarse una conexión: espera unos segundos a que reabra.
      const pedido = /^bytes=(\d+)-/.exec(String(req.headers.range ?? ''))
      const desde = pedido ? Number(pedido[1]) : 0
      const flujo = rep.playback.abrirStream(req.params.sid, propietario(req), desde)
      reply.raw.on('close', () => {
        if (reply.raw.writableFinished) return
        console.info(`[reproducción] conexión del stream cerrada en la sesión ${req.params.sid.slice(0, 6)} (desde el byte ${desde}). ${rep.playback.errorDe(req.params.sid).slice(-200)}`)
      })
      reply.header('Content-Type', 'video/mp4').header('Cache-Control', 'no-store')
      // El tamaño total se desconoce (ffmpeg lo produce al vuelo). Para que Chromium acepte la reapertura hay que declarar un
      // total: uno enorme y fijo (probado: con «*» o respondiendo 200 falla con «data source error»).
      reply.header('Accept-Ranges', 'bytes')
      if (desde > 0) reply.code(206).header('Content-Range', `bytes ${desde}-${TOTAL_FICTICIO - 1}/${TOTAL_FICTICIO}`)
      return reply.send(flujo)
    } catch (e) {
      // Una reapertura que no se puede atender (sesión vencida, byte fuera de la ventana) hace que la app reinicie la sesión: queda anotado.
      if (e instanceof ErrorSesion) console.warn(`[reproducción] stream de la sesión ${req.params.sid.slice(0, 6)} rechazado (${e.status}): ${e.message} Range: ${String(req.headers.range ?? '—')}`)
      return manejarError(reply, e, 'No se pudo abrir el stream')
    }
  })

  app.delete<{ Params: { sid: string } }>('/api/v1/playback/sessions/:sid', async (req, reply) => {
    if (!rep) return sinFfmpeg(reply)
    try {
      rep.playback.detener(req.params.sid, propietario(req))
      return { ok: true }
    } catch (e) {
      return manejarError(reply, e, 'No se pudo detener la sesión')
    }
  })
}
