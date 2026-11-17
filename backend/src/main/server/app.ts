import cors from '@fastify/cors'
import Fastify, { type FastifyInstance } from 'fastify'
import type Database from 'better-sqlite3'
import { AuthRepo } from '../repositories/auth'
import { CatalogoRepo } from '../repositories/catalogo'
import { Escritura } from '../repositories/escritura'
import { Repositorios } from '../repositories/repositorios'
import type { ImageService } from '../services/image-service'
import type { MediaProbeService } from '../services/media-probe'
import { PersonaService } from '../services/persona-service'
import type { PlaybackService } from '../services/playback-service'
import { RUTAS_PUBLICAS, credencialDe, error, resolverPrincipal } from './auth'
import { JobManager } from '../jobs/job-manager'
import { TmdbClient } from '../scanner/tmdb-client'
import { RegistroActividad } from './actividad'
import type { ServidorPublico } from './servidor-publico'
import { rutasAdmin } from './routes/admin'
import { rutasCatalogo } from './routes/catalogo'
import { rutasImagen } from './routes/imagen'
import { rutasReproduccion } from './routes/reproduccion'
import { rutasSistema } from './routes/sistema'
import { rutasUsuario } from './routes/usuario'

export interface ServerDeps {
  db: Database.Database
  images: ImageService
  /** Token efímero del listener interno. Sin él, ningún token admin es válido (listener público). */
  adminToken?: string
  /** Nombre amigable que se informa en `/health` (función: puede cambiar con el servidor corriendo). */
  nombreServidor: string | (() => string)
  /** Clave y certificado: el listener escucha en HTTPS. */
  https?: { key: string; cert: string }
  /** Control del servidor público (solo en el listener interno: lo usan las rutas de administración). */
  servidor?: ServidorPublico
  /** Reproducciones directas en curso; se comparte entre el listener interno y el público. */
  actividad?: RegistroActividad
  /** Carpeta de datos de la colección (solo el listener interno la usa: Ajustes → Datos). */
  carpetaDatos?: string
  /** Base abierta en solo lectura: las escrituras responden 409. */
  soloLectura?: boolean
  /** Biografías de personas (TMDb). Por defecto usa la clave guardada en `app_settings`. */
  personas?: PersonaService
  /** Sonda y sesiones de ffmpeg. Sin esto (falta ffmpeg) solo funciona el modo direct. */
  reproduccion?: ReproduccionDeps
  /** Trabajos de administración (escaneos, metadata). Por defecto uno propio. */
  jobs?: JobManager
  /** Cliente de TMDb para la administración; inyectable para pruebas. */
  tmdb?: TmdbClient
}

export interface ReproduccionDeps {
  probe: MediaProbeService
  playback: PlaybackService
}

export interface Contexto {
  db: Database.Database
  jobs: JobManager
  tmdb: TmdbClient
  repos: Repositorios
  catalogo: CatalogoRepo
  escritura: Escritura
  personas: PersonaService
  reproduccion: ReproduccionDeps | null
  soloLectura: boolean
  auth: AuthRepo
  images: ImageService
  nombreServidor: () => string
  servidor: ServidorPublico | null
  actividad: RegistroActividad
  esInterno: boolean
  carpetaDatos: string | null
}

/** Construye la app Fastify (sin escuchar). `listen` lo hace quien la aloja. */
export async function crearApp(deps: ServerDeps): Promise<FastifyInstance> {
  // forceCloseConnections: el stream SSE de trabajos (y los de video) no están «idle»; sin esto `close()` esperaría para siempre.
  const app = Fastify({ logger: false, forceCloseConnections: true, ...(deps.https ? { https: deps.https } : {}), routerOptions: { ignoreTrailingSlash: true } })
  const ctx: Contexto = {
    db: deps.db,
    jobs: deps.jobs ?? new JobManager(),
    tmdb: deps.tmdb ?? new TmdbClient(),
    repos: new Repositorios(deps.db),
    catalogo: new CatalogoRepo(deps.db),
    escritura: new Escritura(deps.db),
    reproduccion: deps.reproduccion ?? null,
    personas: deps.personas ?? new PersonaService(() => new AuthRepo(deps.db).ajuste('tmdb_api_key')),
    soloLectura: deps.soloLectura ?? false,
    auth: new AuthRepo(deps.db),
    images: deps.images,
    nombreServidor: typeof deps.nombreServidor === 'function' ? deps.nombreServidor : () => deps.nombreServidor as string,
    servidor: deps.servidor ?? null,
    actividad: deps.actividad ?? new RegistroActividad(),
    esInterno: deps.adminToken != null,
    carpetaDatos: deps.carpetaDatos ?? null
  }
  const adminToken = deps.adminToken ?? null

  // El renderer carga desde otro origen (localhost:5173 en dev, file:// empaquetado) y manda `X-Api-Key`: el listener
  // interno necesita CORS, y el público también (el renderer en Modo Cliente habla con un servidor remoto). La API no usa
  // cookies, así que CORS abierto no expone nada: lo que protege es la credencial.
  await app.register(cors, { origin: true, methods: ['GET', 'POST', 'PUT', 'DELETE'], allowedHeaders: ['X-Api-Key', 'Content-Type', 'Range'], exposedHeaders: ['Content-Range', 'Accept-Ranges'] })

  app.decorateRequest('principal', null)
  app.addHook('onRequest', async (req, reply) => {
    if (req.method === 'OPTIONS') return
    const ruta = req.url.split('?')[0].replace(/\/+$/, '')
    if (!ruta.startsWith('/api/v1/') || RUTAS_PUBLICAS.has(ruta)) return
    const credencial = credencialDe(req)
    const principal = credencial ? resolverPrincipal(credencial, ctx.auth, adminToken) : null
    if (!principal) return error(reply, 401, 'Credencial inválida o faltante.')
    req.principal = principal
  })

  app.setNotFoundHandler((_req, reply) => error(reply, 404, 'Ruta no encontrada.'))
  app.setErrorHandler((err: Error & { statusCode?: number }, _req, reply) => {
    const status = err.statusCode && err.statusCode >= 400 && err.statusCode < 500 ? err.statusCode : 500
    return error(reply, status, status === 500 ? 'Error interno del servidor.' : 'Solicitud inválida.')
  })

  rutasSistema(app, ctx)
  rutasCatalogo(app, ctx)
  rutasUsuario(app, ctx)
  rutasReproduccion(app, ctx)
  rutasImagen(app, ctx)
  rutasAdmin(app, ctx)
  app.addHook('onClose', async () => ctx.jobs.cancelarTodos())
  return app
}
