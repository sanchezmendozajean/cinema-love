import BetterSqlite3 from 'better-sqlite3'
import { mkdirSync, rmSync } from 'node:fs'
import { join } from 'node:path'
import schemaV11 from '../db/schema-v11.sql?raw'
import { VERSION_ESQUEMA_DRIFT } from '../db/database'

/**
 * Espejo local del catálogo de un servidor remoto (Modo Cliente de la versión Qt).
 *
 * La interfaz Qt lee el catálogo de un `cinemalove.sqlite`; para el Modo Cliente se construye uno con el MISMO esquema a partir de la
 * API del servidor (a través del proxy local, que ya lleva la credencial): así las vistas son las mismas en ambos modos. Los archivos
 * no existen en este equipo: cada película apunta a su URL de streaming (`<proxy>/api/v1/media/<id>/stream`). Es solo una copia de
 * lectura que se reconstruye en cada conexión; lo que el usuario cambia (progreso, Próximas a ver, colecciones) se envía al servidor.
 *
 * Dos fases para que la interfaz aparezca pronto: (1) listados (títulos, pósters, progreso) y (2) fichas completas (sinopsis, reparto…).
 */

export interface EstadoEspejo {
  fase: 'inactivo' | 'listados' | 'fichas' | 'listo' | 'error'
  hecho: number
  total: number
  /** Ruta del `.sqlite` en cuanto la fase 1 terminó. */
  ruta: string | null
  mensaje: string
}

interface Api {
  baseUrl: string
  token: string
}

type Json = Record<string, unknown>

const seg = (iso: unknown): number => {
  const t = typeof iso === 'string' ? Date.parse(iso) : NaN
  return Number.isFinite(t) ? Math.floor(t / 1000) : Math.floor(Date.now() / 1000)
}
const txt = (v: unknown): string | null => (typeof v === 'string' && v !== '' ? v : null)
const num = (v: unknown): number | null => (typeof v === 'number' && Number.isFinite(v) ? Math.round(v) : null)
const lista = (v: unknown): string => JSON.stringify(Array.isArray(v) ? v : [])

export class Espejo {
  estado: EstadoEspejo = { fase: 'inactivo', hecho: 0, total: 0, ruta: null, mensaje: '' }
  private cancelado = false

  constructor(private readonly carpeta: string, private readonly api: Api, private readonly servidorId: string) {}

  cancelar(): void {
    this.cancelado = true
  }

  private async pedir<T = unknown>(ruta: string): Promise<T | null> {
    try {
      const r = await fetch(this.api.baseUrl + '/api/v1' + ruta, { headers: { 'X-Api-Key': this.api.token } })
      if (!r.ok) return null
      return (await r.json()) as T
    } catch {
      return null
    }
  }

  /** Construye el espejo. Se resuelve al terminar la fase 2; `estado.ruta` está disponible desde la fase 1. */
  async construir(): Promise<void> {
    try {
      const dir = join(this.carpeta, 'espejo')
      mkdirSync(dir, { recursive: true })
      const ruta = join(dir, `${this.servidorId || 'servidor'}.sqlite`)
      for (const ext of ['', '-wal', '-shm', '-journal']) rmSync(ruta + ext, { force: true })
      const db = new BetterSqlite3(ruta)
      db.pragma('journal_mode = WAL')
      db.pragma('busy_timeout = 5000')
      db.exec(schemaV11)
      db.pragma(`user_version = ${VERSION_ESQUEMA_DRIFT}`)
      try {
        this.estado = { ...this.estado, fase: 'listados', hecho: 0, total: 0, mensaje: 'Leyendo el catálogo del servidor…' }
        const { pelis, series } = await this.listados(db)
        if (this.cancelado) return
        db.pragma('wal_checkpoint(PASSIVE)')
        this.estado = { ...this.estado, ruta, fase: 'fichas', hecho: 0, total: pelis.length + series.length, mensaje: 'Descargando fichas completas…' }
        await this.fichas(db, pelis, series)
        if (this.cancelado) return
        this.estado = { ...this.estado, fase: 'listo', mensaje: '' }
      } finally {
        db.pragma('wal_checkpoint(TRUNCATE)')
        db.close()
      }
    } catch (e) {
      this.estado = { ...this.estado, fase: 'error', mensaje: e instanceof Error ? e.message : String(e) }
    }
  }

  private async listados(db: BetterSqlite3.Database): Promise<{ pelis: string[]; series: string[] }> {
    const streamBase = `${this.api.baseUrl}/api/v1/media`
    const bibs = await this.pedir<Json[]>('/libraries')
    if (!bibs) throw new Error('No se pudo leer las bibliotecas del servidor.')
    const insBib = db.prepare('INSERT INTO libraries (id, nombre, tipo, ruta_actual, creado_en) VALUES (?, ?, ?, ?, ?)')
    const insItem = db.prepare(
      `INSERT INTO media_items (id, library_id, ruta_relativa, estado_archivo, tmdb_id, titulo, anio, poster_url, backdrop_url, duracion_segundos, generos, creado_en, series_id, numero_temporada, numero_episodio, sinopsis)
       VALUES (?, ?, ?, 'ok', ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`
    )
    const insSerie = db.prepare(
      `INSERT INTO series (id, library_id, ruta_relativa, estado_archivo, tmdb_id, titulo, anio, poster_url, backdrop_url, generos, creado_en)
       VALUES (?, ?, ?, 'ok', ?, ?, ?, ?, ?, ?, ?)`
    )
    const insTemp = db.prepare('INSERT OR IGNORE INTO seasons (id, series_id, numero_temporada, titulo, sinopsis, poster_url) VALUES (?, ?, ?, ?, ?, ?)')
    const pelis: string[] = []
    const series: string[] = []
    const episodios: string[] = []
    const itemDe = (m: Json, lib: string): unknown[] => [
      m.id, lib, `${m.id}/stream?api_key=${this.api.token}`, txt(m.tmdbId), txt(m.titulo) ?? txt(m.tituloArchivo), num(m.anio), txt(m.posterUrl), txt(m.backdropUrl),
      num(m.duracionSegundos), lista(m.generos), seg(m.creadoEn), txt(m.seriesId), num(m.numeroTemporada), num(m.numeroEpisodio), txt(m.sinopsis)
    ]
    for (const b of bibs) {
      if (this.cancelado) return { pelis, series }
      const lib = String(b.id)
      insBib.run(lib, b.nombre, b.tipo, streamBase, seg(b.creadoEn))
      if (b.tipo === 'pelicula') {
        for (const m of (await this.pedir<Json[]>(`/libraries/${lib}/items`)) ?? []) {
          insItem.run(...itemDe(m, lib))
          pelis.push(String(m.id))
        }
      } else {
        for (const s of (await this.pedir<Json[]>(`/libraries/${lib}/series`)) ?? []) {
          const sid = String(s.id)
          insSerie.run(sid, lib, sid, txt(s.tmdbId), txt(s.titulo) ?? txt(s.tituloArchivo), num(s.anio), txt(s.posterUrl), txt(s.backdropUrl), lista(s.generos), seg(s.creadoEn))
          series.push(sid)
          for (const t of (await this.pedir<Json[]>(`/series/${sid}/seasons`)) ?? [])
            insTemp.run(t.id, sid, num(t.numeroTemporada) ?? 0, txt(t.titulo), txt(t.sinopsis), txt(t.posterUrl))
          for (const e of (await this.pedir<Json[]>(`/series/${sid}/episodes`)) ?? []) {
            insItem.run(...itemDe({ ...e, seriesId: sid }, lib))
            episodios.push(String(e.id))
          }
        }
      }
      this.estado = { ...this.estado, hecho: pelis.length + series.length }
    }
    await this.progreso(db, [...pelis, ...episodios])
    return { pelis, series }
  }

  /** Vuelve a leer del servidor solo lo personal (progreso y «Próximas a ver») y lo reescribe en el espejo: rápido, tras una acción del usuario. */
  async actualizarPersonal(): Promise<void> {
    const ruta = this.estado.ruta
    if (!ruta || this.estado.fase === 'listados') return
    const db = new BetterSqlite3(ruta)
    try {
      db.pragma('busy_timeout = 5000')
      const ids = (db.prepare('SELECT id FROM media_items').all() as { id: string }[]).map((r) => r.id)
      db.transaction(() => { db.exec('DELETE FROM watch_progresses; DELETE FROM proximas_a_ver;') })()
      await this.progreso(db, ids)
    } finally {
      db.close()
    }
  }

  /** Progreso y «Próximas a ver» del usuario en el servidor, copiados al espejo (solo para mostrar; el servidor es la fuente). */
  private async progreso(db: BetterSqlite3.Database, ids: string[]): Promise<void> {
    const ahora = Math.floor(Date.now() / 1000)
    const existentes = new Set(ids)
    const insProg = db.prepare('INSERT INTO watch_progresses (media_item_id, usuario_id, posicion_segundos, estado, ultima_reproduccion, pista_audio_id, pista_subtitulo_id) VALUES (?, NULL, ?, ?, ?, ?, ?)')
    const home = await this.pedir<Json>('/home')
    const orden = new Map<string, number>()
    const cont = Array.isArray(home?.continuarViendo) ? (home!.continuarViendo as Json[]) : []
    cont.forEach((c, i) => orden.set(String(c.id), ahora - i * 60))
    const bibs = (db.prepare('SELECT id, tipo FROM libraries').all() as { id: string; tipo: string }[])
    const estados = new Map<string, string>()
    for (const b of bibs) {
      const r = await this.pedir<Record<string, string>>(`/libraries/${b.id}/progress-states`)
      for (const [id, e] of Object.entries(r ?? {})) estados.set(id, e)
    }
    for (const s of db.prepare('SELECT id FROM series').all() as { id: string }[]) {
      const r = await this.pedir<Record<string, string>>(`/series/${s.id}/progress-states`)
      for (const [id, e] of Object.entries(r ?? {})) estados.set(id, e)
    }
    const items = [...estados.entries()].filter(([id, e]) => existentes.has(id) && (e === 'visto' || e === 'enProgreso'))
    let i = 0
    const trabajar = async (): Promise<void> => {
      while (i < items.length) {
        const [id, estado] = items[i++]
        let pos = 0, audio: string | null = null, sub: string | null = null
        if (estado === 'enProgreso') {
          const p = await this.pedir<Json>(`/media/${id}/progress`)
          pos = num(p?.posicionSegundos) ?? 0
          audio = txt(p?.pistaAudioId)
          sub = txt(p?.pistaSubtituloId)
        }
        insProg.run(id, pos, estado, orden.get(id) ?? ahora - 86400 * 30, audio, sub)
      }
    }
    await Promise.all(Array.from({ length: 8 }, trabajar))
    const wl = await this.pedir<Json[]>('/watchlist')
    const insWl = db.prepare('INSERT INTO proximas_a_ver (media_item_id, usuario_id, agregado_en) VALUES (?, NULL, ?)')
    ;(wl ?? []).forEach((m, k) => { if (existentes.has(String(m.id))) insWl.run(String(m.id), ahora - k * 60) })
  }

  private async fichas(db: BetterSqlite3.Database, pelis: string[], series: string[]): Promise<void> {
    const upItem = db.prepare('UPDATE media_items SET titulo = COALESCE(titulo, ?), sinopsis = ?, titulo_original = ?, idioma_original = ?, estudio = ?, reparto = ?, equipo = ? WHERE id = ?')
    const upSerie = db.prepare('UPDATE series SET titulo = COALESCE(titulo, ?), sinopsis = ?, titulo_original = ?, idioma_original = ?, estudio = ?, reparto = ?, equipo = ? WHERE id = ?')
    const trabajos: { ruta: string; up: BetterSqlite3.Statement; id: string }[] = [
      ...pelis.map((id) => ({ ruta: `/items/${id}`, up: upItem, id })),
      ...series.map((id) => ({ ruta: `/series/${id}`, up: upSerie, id }))
    ]
    let i = 0
    const trabajar = async (): Promise<void> => {
      while (i < trabajos.length && !this.cancelado) {
        const t = trabajos[i++]
        const d = await this.pedir<Json>(t.ruta)
        if (d) t.up.run(txt(d.tituloArchivo), txt(d.sinopsis), txt(d.tituloOriginal), txt(d.idiomaOriginal), txt(d.estudio), lista(d.reparto), lista(d.equipo), t.id)
        this.estado = { ...this.estado, hecho: this.estado.hecho + 1 }
      }
    }
    await Promise.all(Array.from({ length: 10 }, trabajar))
  }
}
