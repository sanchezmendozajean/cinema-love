import BetterSqlite3 from 'better-sqlite3'
import { randomUUID } from 'node:crypto'
import schemaCliente from '../db/schema-cliente-v6.sql?raw'
import { estadoVistoDesdeDb } from '../db/codecs'
import type { EstadoVisto } from '../../shared/enums'

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type Fila = Record<string, any>

export interface ProgresoLocal {
  posicionSegundos: number
  estado: EstadoVisto
  pistaAudioId: string | null
  pistaSubtituloId: string | null
}

export interface ColeccionLocal {
  id: string
  nombre: string
  quitarAlVerse: boolean
  /** Segundos desde epoch. */
  creadoEn: number
}

const ahora = (): number => Math.floor(Date.now() / 1000)

const aProgreso = (r: Fila): ProgresoLocal => ({
  posicionSegundos: r.posicion_segundos,
  estado: estadoVistoDesdeDb(r.estado),
  pistaAudioId: r.pista_audio_id,
  pistaSubtituloId: r.pista_subtitulo_id
})

/**
 * Lo personal de ESTE equipo cuando se conecta a un servidor con token (specs/03 §5): progreso, «Próximas a ver» y
 * colecciones, en las tablas `cliente_*` de `cliente_datos.sqlite`, separado por servidor. Los ids de ítem son los del
 * servidor remoto. Misma semántica que `Escritura` del servidor (una fila por ítem; «no visto» = sin fila).
 */
export class EstadoLocal {
  constructor(
    private readonly db: BetterSqlite3.Database,
    private readonly servidorId: string
  ) {}

  /**
   * Para una conexión que no se recuerda: vive solo mientras dura la sesión (nada se guarda en disco, porque no hay un
   * servidor guardado al que asociarlo).
   */
  static enMemoria(): EstadoLocal {
    const db = new BetterSqlite3(':memory:')
    db.pragma('foreign_keys = ON')
    db.exec(schemaCliente)
    db.prepare("INSERT INTO cliente_servidores (id, host, puerto) VALUES ('sesion', 'sesion', 0)").run()
    return new EstadoLocal(db, 'sesion')
  }

  // ------------------------------------------------------------------ progreso global

  progreso(mediaItemId: string): ProgresoLocal | null {
    const r = this.db.prepare('SELECT * FROM cliente_progresos WHERE servidor_id = ? AND media_item_id = ?').get(this.servidorId, mediaItemId) as Fila | undefined
    return r ? aProgreso(r) : null
  }

  /** Upsert conservando las pistas elegidas si no se mandan nuevas. */
  guardarProgreso(mediaItemId: string, posicionSegundos: number, estado: EstadoVisto, audio?: string | null, subtitulo?: string | null): void {
    this.db
      .prepare(
        `INSERT INTO cliente_progresos (servidor_id, media_item_id, posicion_segundos, estado, ultima_reproduccion, pista_audio_id, pista_subtitulo_id)
         VALUES (@s, @m, @p, @e, @t, @a, @u)
         ON CONFLICT (servidor_id, media_item_id) DO UPDATE SET
           posicion_segundos = @p, estado = @e, ultima_reproduccion = @t,
           pista_audio_id = COALESCE(@a, pista_audio_id), pista_subtitulo_id = COALESCE(@u, pista_subtitulo_id)`
      )
      .run({ s: this.servidorId, m: mediaItemId, p: posicionSegundos, e: estado, t: ahora(), a: audio ?? null, u: subtitulo ?? null })
  }

  /** Visto: posición 0 y se retira de Próximas a ver. */
  marcarVisto(mediaItemId: string): void {
    this.db.transaction(() => {
      this.guardarProgreso(mediaItemId, 0, 'visto')
      this.quitarDeProximas(mediaItemId)
    })()
  }

  marcarNoVisto(mediaItemId: string): void {
    this.db.prepare('DELETE FROM cliente_progresos WHERE servidor_id = ? AND media_item_id = ?').run(this.servidorId, mediaItemId)
  }

  /** Marca varios ítems de golpe (una temporada o una serie). */
  marcarVarios(ids: string[], estado: 'visto' | 'noVisto'): void {
    this.db.transaction(() => {
      for (const id of ids) {
        if (estado === 'visto') this.marcarVisto(id)
        else this.marcarNoVisto(id)
      }
    })()
  }

  /** Estado de todo lo que no es «no visto». Sirve para las pantallas por biblioteca/serie (las claves son ids únicos). */
  estados(): Record<string, { estado: EstadoVisto; posicionSegundos: number }> {
    const filas = this.db.prepare("SELECT media_item_id AS id, estado, posicion_segundos AS pos FROM cliente_progresos WHERE servidor_id = ? AND estado != 'noVisto'").all(this.servidorId) as Fila[]
    return Object.fromEntries(filas.map((f) => [f.id as string, { estado: estadoVistoDesdeDb(f.estado), posicionSegundos: f.pos as number }]))
  }

  /** Todo el progreso guardado (lo que se manda al servidor para que arme el catálogo con ESTE progreso). */
  filas(): { id: string; estado: EstadoVisto; posicionSegundos: number; ultima: number | null }[] {
    return (this.db.prepare("SELECT media_item_id AS id, estado, posicion_segundos AS pos, ultima_reproduccion AS ultima FROM cliente_progresos WHERE servidor_id = ? AND estado != 'noVisto'").all(this.servidorId) as Fila[]).map((f) => ({
      id: f.id as string,
      estado: estadoVistoDesdeDb(f.estado),
      posicionSegundos: f.pos as number,
      ultima: f.ultima as number | null
    }))
  }

  /** Ítems en progreso, el más reciente primero. */
  enProgreso(limite = 20): { id: string; posicionSegundos: number }[] {
    return (this.db.prepare("SELECT media_item_id AS id, posicion_segundos AS pos FROM cliente_progresos WHERE servidor_id = ? AND estado = 'enProgreso' ORDER BY ultima_reproduccion DESC, id DESC LIMIT ?").all(this.servidorId, limite) as Fila[]).map((r) => ({ id: r.id as string, posicionSegundos: r.pos as number }))
  }

  vistosRecientes(limite = 60): string[] {
    return (this.db.prepare("SELECT media_item_id AS id FROM cliente_progresos WHERE servidor_id = ? AND estado = 'visto' ORDER BY ultima_reproduccion DESC, id DESC LIMIT ?").all(this.servidorId, limite) as Fila[]).map((r) => r.id as string)
  }

  // ------------------------------------------------------------------ Próximas a ver

  enProximas(mediaItemId: string): boolean {
    return !!this.db.prepare('SELECT 1 FROM cliente_proximas_a_ver WHERE servidor_id = ? AND media_item_id = ?').get(this.servidorId, mediaItemId)
  }

  agregarAProximas(mediaItemId: string): void {
    this.db.prepare('INSERT OR IGNORE INTO cliente_proximas_a_ver (servidor_id, media_item_id, agregado_en) VALUES (?, ?, ?)').run(this.servidorId, mediaItemId, ahora())
  }

  quitarDeProximas(mediaItemId: string): void {
    this.db.prepare('DELETE FROM cliente_proximas_a_ver WHERE servidor_id = ? AND media_item_id = ?').run(this.servidorId, mediaItemId)
  }

  /** Ids de Próximas a ver, del más reciente al más antiguo. */
  proximas(limite = 500): string[] {
    return (this.db.prepare('SELECT media_item_id AS id FROM cliente_proximas_a_ver WHERE servidor_id = ? ORDER BY agregado_en DESC, id DESC LIMIT ?').all(this.servidorId, limite) as Fila[]).map((r) => r.id as string)
  }

  // ------------------------------------------------------------------ colecciones

  private aColeccion(r: Fila): ColeccionLocal {
    return { id: r.id, nombre: r.nombre, quitarAlVerse: r.quitar_al_verse === 1, creadoEn: r.creado_en }
  }

  listarColecciones(): ColeccionLocal[] {
    return (this.db.prepare('SELECT * FROM cliente_colecciones WHERE servidor_id = ? ORDER BY nombre COLLATE NOCASE').all(this.servidorId) as Fila[]).map((r) => this.aColeccion(r))
  }

  obtenerColeccion(id: string): ColeccionLocal | null {
    const r = this.db.prepare('SELECT * FROM cliente_colecciones WHERE id = ? AND servidor_id = ?').get(id, this.servidorId) as Fila | undefined
    return r ? this.aColeccion(r) : null
  }

  crearColeccion(nombre: string, quitarAlVerse: boolean): string {
    const id = randomUUID()
    this.db.prepare('INSERT INTO cliente_colecciones (id, servidor_id, nombre, quitar_al_verse, creado_en) VALUES (?, ?, ?, ?, ?)').run(id, this.servidorId, nombre.trim(), quitarAlVerse ? 1 : 0, ahora())
    return id
  }

  renombrarColeccion(id: string, nombre: string): void {
    this.db.prepare('UPDATE cliente_colecciones SET nombre = ? WHERE id = ? AND servidor_id = ?').run(nombre.trim(), id, this.servidorId)
  }

  setQuitarAlVerse(id: string, valor: boolean): void {
    this.db.prepare('UPDATE cliente_colecciones SET quitar_al_verse = ? WHERE id = ? AND servidor_id = ?').run(valor ? 1 : 0, id, this.servidorId)
  }

  /** Borra la colección (items y progresos propios caen por ON DELETE CASCADE; las películas no se tocan). */
  eliminarColeccion(id: string): void {
    this.db.prepare('DELETE FROM cliente_colecciones WHERE id = ? AND servidor_id = ?').run(id, this.servidorId)
  }

  coleccionesDeItem(mediaItemId: string): ColeccionLocal[] {
    return (this.db.prepare('SELECT c.* FROM cliente_colecciones c JOIN cliente_coleccion_items ci ON ci.coleccion_id = c.id WHERE ci.media_item_id = ? AND c.servidor_id = ? ORDER BY c.nombre COLLATE NOCASE').all(mediaItemId, this.servidorId) as Fila[]).map((r) => this.aColeccion(r))
  }

  /** Ids de los ítems de una colección, del más reciente al más antiguo. */
  idsDeColeccion(coleccionId: string): string[] {
    return (this.db.prepare('SELECT media_item_id AS id FROM cliente_coleccion_items WHERE coleccion_id = ? ORDER BY agregado_en DESC, id DESC').all(coleccionId) as Fila[]).map((r) => r.id as string)
  }

  agregarItemAColeccion(coleccionId: string, mediaItemId: string): void {
    this.db.prepare('INSERT OR IGNORE INTO cliente_coleccion_items (coleccion_id, media_item_id, agregado_en) VALUES (?, ?, ?)').run(coleccionId, mediaItemId, ahora())
  }

  quitarItemDeColeccion(coleccionId: string, mediaItemId: string): void {
    this.db.prepare('DELETE FROM cliente_coleccion_items WHERE coleccion_id = ? AND media_item_id = ?').run(coleccionId, mediaItemId)
  }

  // ------------------------------------------------------------------ progreso dentro de una colección

  progresoEnColeccion(coleccionId: string, mediaItemId: string): ProgresoLocal | null {
    const r = this.db.prepare('SELECT * FROM cliente_coleccion_progresos WHERE coleccion_id = ? AND media_item_id = ?').get(coleccionId, mediaItemId) as Fila | undefined
    return r ? aProgreso(r) : null
  }

  guardarProgresoEnColeccion(coleccionId: string, mediaItemId: string, posicionSegundos: number, estado: EstadoVisto, audio?: string | null, subtitulo?: string | null): void {
    this.db
      .prepare(
        `INSERT INTO cliente_coleccion_progresos (coleccion_id, media_item_id, posicion_segundos, estado, ultima_reproduccion, pista_audio_id, pista_subtitulo_id)
         VALUES (@c, @m, @p, @e, @t, @a, @u)
         ON CONFLICT (coleccion_id, media_item_id) DO UPDATE SET
           posicion_segundos = @p, estado = @e, ultima_reproduccion = @t,
           pista_audio_id = COALESCE(@a, pista_audio_id), pista_subtitulo_id = COALESCE(@u, pista_subtitulo_id)`
      )
      .run({ c: coleccionId, m: mediaItemId, p: posicionSegundos, e: estado, t: ahora(), a: audio ?? null, u: subtitulo ?? null })
  }

  /** Visto dentro de la colección; con «quitar al verse» el ítem sale de ella. */
  marcarVistoEnColeccion(coleccionId: string, mediaItemId: string): void {
    this.db.transaction(() => {
      this.guardarProgresoEnColeccion(coleccionId, mediaItemId, 0, 'visto')
      if (this.obtenerColeccion(coleccionId)?.quitarAlVerse) this.quitarItemDeColeccion(coleccionId, mediaItemId)
    })()
  }

  marcarNoVistoEnColeccion(coleccionId: string, mediaItemId: string): void {
    this.db.prepare('DELETE FROM cliente_coleccion_progresos WHERE coleccion_id = ? AND media_item_id = ?').run(coleccionId, mediaItemId)
  }

  continuarEnColeccion(coleccionId: string): { id: string; posicionSegundos: number }[] {
    return (this.db.prepare("SELECT media_item_id AS id, posicion_segundos AS pos FROM cliente_coleccion_progresos WHERE coleccion_id = ? AND estado = 'enProgreso' ORDER BY ultima_reproduccion DESC").all(coleccionId) as Fila[]).map((r) => ({ id: r.id as string, posicionSegundos: r.pos as number }))
  }

  progresosDeColeccion(coleccionId: string): Map<string, ProgresoLocal> {
    const filas = this.db.prepare('SELECT * FROM cliente_coleccion_progresos WHERE coleccion_id = ?').all(coleccionId) as Fila[]
    return new Map(filas.map((f) => [f.media_item_id as string, aProgreso(f)]))
  }
}
