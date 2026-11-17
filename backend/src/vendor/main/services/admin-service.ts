import { randomBytes, randomUUID } from 'node:crypto'
import { existsSync, readdirSync, renameSync, statSync } from 'node:fs'
import { extname, isAbsolute, join, relative, resolve } from 'node:path'
import bcrypt from 'bcryptjs'
import type Database from 'better-sqlite3'
import { ahoraDb } from '../db/codecs'
import { TIPOS_BIBLIOTECA, type TipoBiblioteca } from '../../shared/enums'
import type { InfoArchivo } from '../../shared/admin-types'
import { hashDeRespaldo } from '../scanner/escaner'

/** Error de validación con el mensaje que ve la persona. `status` es el HTTP que le corresponde. */
export class ErrorAdmin extends Error {
  constructor(
    mensaje: string,
    readonly status = 400
  ) {
    super(mensaje)
  }
}

export interface BibliotecaAdmin {
  id: string
  nombre: string
  tipo: TipoBiblioteca
  ruta: string
  disponible: boolean
  items: number
  series: number
  huerfanos: number
}

export interface Huerfano {
  tipo: 'item' | 'serie'
  id: string
  rutaRelativa: string
  titulo: string | null
  /** Para episodios: nombre de la serie. */
  serie: string | null
}

const esCarpeta = (ruta: string): boolean => {
  try {
    return statSync(ruta).isDirectory()
  } catch {
    return false
  }
}

function requerirBiblioteca(db: Database.Database, id: string): { id: string; nombre: string; tipo: TipoBiblioteca; ruta_actual: string } {
  const b = db.prepare('SELECT id, nombre, tipo, ruta_actual FROM libraries WHERE id = ?').get(id) as { id: string; nombre: string; tipo: TipoBiblioteca; ruta_actual: string } | undefined
  if (!b) throw new ErrorAdmin('La biblioteca no existe.', 404)
  return b
}

export function listarBibliotecas(db: Database.Database): BibliotecaAdmin[] {
  const filas = db.prepare('SELECT id, nombre, tipo, ruta_actual FROM libraries ORDER BY creado_en, nombre').all() as { id: string; nombre: string; tipo: TipoBiblioteca; ruta_actual: string }[]
  return filas.map((b) => {
    const n = (sql: string): number => (db.prepare(sql).get(b.id) as { n: number }).n
    return {
      id: b.id,
      nombre: b.nombre,
      tipo: b.tipo,
      ruta: b.ruta_actual,
      disponible: esCarpeta(b.ruta_actual),
      items: n("SELECT COUNT(*) n FROM media_items WHERE library_id = ? AND estado_archivo = 'ok'"),
      series: n("SELECT COUNT(*) n FROM series WHERE library_id = ? AND estado_archivo = 'ok'"),
      huerfanos: n("SELECT COUNT(*) n FROM media_items WHERE library_id = ? AND estado_archivo != 'ok'") + n("SELECT COUNT(*) n FROM series WHERE library_id = ? AND estado_archivo != 'ok'")
    }
  })
}

function validarNombre(db: Database.Database, nombre: unknown, exceptoId?: string): string {
  if (typeof nombre !== 'string' || nombre.trim() === '') throw new ErrorAdmin('El nombre de la biblioteca es obligatorio.')
  const n = nombre.trim()
  const repetido = db.prepare('SELECT id FROM libraries WHERE lower(nombre) = lower(?) AND id != ?').get(n, exceptoId ?? '')
  if (repetido) throw new ErrorAdmin('Ya existe una biblioteca con ese nombre.', 409)
  return n
}

function validarRuta(db: Database.Database, ruta: unknown, exceptoId?: string): string {
  if (typeof ruta !== 'string' || ruta.trim() === '') throw new ErrorAdmin('Elige la carpeta de la biblioteca.')
  const r = resolve(ruta.trim())
  if (!esCarpeta(r)) throw new ErrorAdmin('La carpeta no existe o no es accesible.')
  const otras = db.prepare('SELECT id, nombre, ruta_actual FROM libraries WHERE id != ?').all(exceptoId ?? '') as { nombre: string; ruta_actual: string }[]
  for (const o of otras) {
    const a = resolve(o.ruta_actual).toLowerCase()
    const b = r.toLowerCase()
    if (a === b) throw new ErrorAdmin(`Esa carpeta ya pertenece a la biblioteca «${o.nombre}».`, 409)
    // Una dentro de otra duplicaría los archivos en dos bibliotecas.
    if (b.startsWith(join(a, '/').toLowerCase()) || a.startsWith(join(b, '/').toLowerCase())) throw new ErrorAdmin(`La carpeta se superpone con la biblioteca «${o.nombre}».`, 409)
  }
  return r
}

export function crearBiblioteca(db: Database.Database, d: { nombre?: unknown; tipo?: unknown; ruta?: unknown }): string {
  if (!(TIPOS_BIBLIOTECA as readonly unknown[]).includes(d.tipo)) throw new ErrorAdmin('Tipo de biblioteca inválido.')
  const nombre = validarNombre(db, d.nombre)
  const ruta = validarRuta(db, d.ruta)
  const id = randomUUID()
  db.prepare('INSERT INTO libraries (id, nombre, tipo, ruta_actual, creado_en) VALUES (?, ?, ?, ?, ?)').run(id, nombre, d.tipo, ruta, ahoraDb())
  return id
}

/** Devuelve si cambió la carpeta (en ese caso hay que reescanear para reconciliar por hash). El tipo no se edita. */
export function actualizarBiblioteca(db: Database.Database, id: string, d: { nombre?: unknown; ruta?: unknown }): { rutaCambio: boolean } {
  const b = requerirBiblioteca(db, id)
  const nombre = d.nombre === undefined ? b.nombre : validarNombre(db, d.nombre, id)
  const ruta = d.ruta === undefined ? b.ruta_actual : validarRuta(db, d.ruta, id)
  db.prepare('UPDATE libraries SET nombre = ?, ruta_actual = ? WHERE id = ?').run(nombre, ruta, id)
  return { rutaCambio: resolve(ruta).toLowerCase() !== resolve(b.ruta_actual).toLowerCase() }
}

/** Quita la biblioteca y su catálogo (no toca archivos). Exige escribir el nombre exacto: borra progreso e historial. */
export function eliminarBiblioteca(db: Database.Database, id: string, confirmar: unknown): void {
  const b = requerirBiblioteca(db, id)
  if (confirmar !== b.nombre) throw new ErrorAdmin('Escribe el nombre exacto de la biblioteca para confirmar.', 409)
  db.transaction(() => {
    // Orden explícito: no se depende de que todas las FK del esquema heredado tengan ON DELETE CASCADE.
    db.prepare('DELETE FROM media_items WHERE library_id = ?').run(id)
    db.prepare('DELETE FROM series WHERE library_id = ?').run(id)
    db.prepare('DELETE FROM libraries WHERE id = ?').run(id)
  })()
}

export function listarHuerfanos(db: Database.Database, bibliotecaId: string): Huerfano[] {
  requerirBiblioteca(db, bibliotecaId)
  const items = db
    .prepare(
      `SELECT m.id, m.ruta_relativa, m.titulo, s.titulo AS serie_titulo, s.ruta_relativa AS serie_ruta
       FROM media_items m LEFT JOIN series s ON s.id = m.series_id
       WHERE m.library_id = ? AND m.estado_archivo != 'ok' ORDER BY m.ruta_relativa`
    )
    .all(bibliotecaId) as { id: string; ruta_relativa: string; titulo: string | null; serie_titulo: string | null; serie_ruta: string | null }[]
  const series = db.prepare("SELECT id, ruta_relativa, titulo FROM series WHERE library_id = ? AND estado_archivo != 'ok' ORDER BY ruta_relativa").all(bibliotecaId) as { id: string; ruta_relativa: string; titulo: string | null }[]
  return [
    ...items.map((i): Huerfano => ({ tipo: 'item', id: i.id, rutaRelativa: i.ruta_relativa, titulo: i.titulo, serie: i.serie_titulo ?? i.serie_ruta })),
    ...series.map((s): Huerfano => ({ tipo: 'serie', id: s.id, rutaRelativa: s.ruta_relativa, titulo: s.titulo, serie: null }))
  ]
}

/** Borra los ítems/series sin archivo. Con `confirmar` = cantidad exacta que se le mostró a la persona. */
export function eliminarHuerfanos(db: Database.Database, bibliotecaId: string, confirmar: unknown): number {
  const lista = listarHuerfanos(db, bibliotecaId)
  if (Number(confirmar) !== lista.length || lista.length === 0) throw new ErrorAdmin('La cantidad a eliminar cambió; revisa la lista y confirma de nuevo.', 409)
  db.transaction(() => {
    for (const h of lista) db.prepare(`DELETE FROM ${h.tipo === 'item' ? 'media_items' : 'series'} WHERE id = ?`).run(h.id)
  })()
  return lista.length
}

/**
 * Re-vincula un ítem sin archivo con otro archivo que sí existe en la carpeta de su biblioteca. Conserva TODO lo que el
 * ítem ya tenía (metadata, progreso, colecciones): solo cambia dónde está el archivo.
 */
export async function revincular(db: Database.Database, itemId: string, relativa: unknown): Promise<void> {
  if (typeof relativa !== 'string' || relativa.trim() === '') throw new ErrorAdmin('Indica la ruta del archivo.')
  const item = db.prepare('SELECT id, library_id FROM media_items WHERE id = ?').get(itemId) as { id: string; library_id: string } | undefined
  if (!item) throw new ErrorAdmin('El ítem no existe.', 404)
  const lib = requerirBiblioteca(db, item.library_id)
  const raiz = resolve(lib.ruta_actual)
  const abs = resolve(raiz, relativa)
  const rel = relative(raiz, abs)
  // Que el archivo esté DENTRO de la biblioteca (evita `..` y rutas absolutas hacia otro lado del disco).
  if (rel === '' || rel.startsWith('..') || isAbsolute(rel)) throw new ErrorAdmin('El archivo debe estar dentro de la carpeta de la biblioteca.')
  if (!existsSync(abs) || !statSync(abs).isFile()) throw new ErrorAdmin('El archivo no existe.', 404)
  const nueva = rel.split(/[\\/]/).join('/')
  const ocupada = db.prepare("SELECT id FROM media_items WHERE library_id = ? AND ruta_relativa = ? AND estado_archivo = 'ok' AND id != ?").get(lib.id, nueva, itemId)
  if (ocupada) throw new ErrorAdmin('Ese archivo ya está vinculado a otro ítem.', 409)
  const hash = await hashDeRespaldo(abs)
  db.prepare("UPDATE media_items SET ruta_relativa = ?, hash_respaldo = ?, estado_archivo = 'ok' WHERE id = ?").run(nueva, hash, itemId)
}

/** Clave de TMDb enmascarada para mostrarla (`••••••••abcd`); nunca se devuelve completa. */
export const enmascarar = (clave: string | null): string | null => (clave ? `${'•'.repeat(8)}${clave.slice(-4)}` : null)

// ------------------------------------------------------------------ homologar nombre

export interface PlanHomologar {
  actual: string
  nuevo: string
  /** Archivos que acompañan a la película (subtítulos externos, etc.) y se renombran con ella. */
  acompanantes: number
  extras: number
}

/** Quita lo que Windows no admite en nombres de archivo y los puntos/espacios finales. */
const PROHIBIDOS = new Set(['<', '>', ':', '"', '/', String.fromCharCode(92), '|', '?', '*'])
const limpiarNombre = (t: string): string =>
  [...t]
    .map((c) => (PROHIBIDOS.has(c) || c.charCodeAt(0) < 32 ? ' ' : c))
    .join('')
    .replace(/\s+/g, ' ')
    .replace(/[. ]+$/, '')
    .trim()

/**
 * `Título (Año)/Título (Año).ext`. Calcula el plan sin tocar el disco; con `aplicar` renombra carpeta y archivo (más los
 * subtítulos externos de su mismo nombre) y actualiza `ruta_relativa` del ítem y de sus extras. Si algo falla a medias,
 * deshace todo lo renombrado. Solo películas con la estructura `Carpeta/Archivo`.
 */
export function homologarNombre(db: Database.Database, itemId: string, d: { titulo?: unknown; anio?: unknown }, aplicar: boolean): PlanHomologar {
  const item = db.prepare('SELECT id, library_id, series_id, ruta_relativa, titulo, anio FROM media_items WHERE id = ?').get(itemId) as
    | { id: string; library_id: string; series_id: string | null; ruta_relativa: string; titulo: string | null; anio: number | null }
    | undefined
  if (!item) throw new ErrorAdmin('El ítem no existe.', 404)
  if (item.series_id) throw new ErrorAdmin('Homologar el nombre solo aplica a películas.')
  const lib = requerirBiblioteca(db, item.library_id)

  const tituloBruto = typeof d.titulo === 'string' ? d.titulo : (item.titulo ?? '')
  const titulo = limpiarNombre(tituloBruto)
  if (!titulo) throw new ErrorAdmin('Para homologar el nombre la película necesita un título.')
  const anioNum = d.anio === undefined ? item.anio : d.anio === null || d.anio === '' ? null : Number(d.anio)
  if (anioNum != null && (!Number.isInteger(anioNum) || anioNum < 1800 || anioNum > 2200)) throw new ErrorAdmin('El año no es válido.')
  const base = anioNum != null ? `${titulo} (${anioNum})` : titulo

  const partes = item.ruta_relativa.split('/')
  if (partes.length !== 2) throw new ErrorAdmin('La película no está en una carpeta propia (Carpeta/Archivo): no se puede homologar automáticamente.')
  const [carpetaVieja, archivoViejo] = partes
  const ext = extname(archivoViejo)
  const baseVieja = archivoViejo.slice(0, archivoViejo.length - ext.length)
  const nuevo = `${base}/${base}${ext}`
  if (nuevo === item.ruta_relativa) throw new ErrorAdmin('El nombre ya está homologado.', 409)

  const raiz = resolve(lib.ruta_actual)
  const absCarpetaVieja = join(raiz, carpetaVieja)
  const absCarpetaNueva = join(raiz, base)
  if (!esCarpeta(absCarpetaVieja)) throw new ErrorAdmin('La carpeta de la película no existe en el disco.', 404)

  // `substr` y no `LIKE`: los nombres de carpeta pueden llevar % o _ y habría que escaparlos.
  const prefijo = `${carpetaVieja}/`
  const otros = db.prepare('SELECT 1 FROM media_items WHERE library_id = ? AND id != ? AND substr(ruta_relativa, 1, ?) = ?').get(lib.id, itemId, prefijo.length, prefijo)
  if (otros) throw new ErrorAdmin('Hay otra película en esa misma carpeta: renombrarla afectaría a ambas.', 409)
  // Mismo nombre salvo mayúsculas (Windows) no es colisión: es la misma carpeta.
  const mismaCarpeta = absCarpetaVieja.toLowerCase() === absCarpetaNueva.toLowerCase()
  if (!mismaCarpeta && existsSync(absCarpetaNueva)) throw new ErrorAdmin(`Ya existe una carpeta llamada «${base}».`, 409)

  // Acompañantes: archivos de la carpeta con el mismo nombre base (Alien.es.srt → Nuevo.es.srt), distintos del principal.
  const acompanantes = readdirSync(absCarpetaVieja, { withFileTypes: true })
    .filter((e) => e.isFile() && e.name !== archivoViejo && e.name.toLowerCase().startsWith(`${baseVieja.toLowerCase()}.`))
    .map((e) => e.name)
  // En Windows `rename` sobrescribe sin avisar: si un destino ya existe dentro de la carpeta (y no es el mismo archivo con otras mayúsculas) se aborta.
  const renombres: [string, string][] = [[archivoViejo, `${base}${ext}`], ...acompanantes.map((a): [string, string] => [a, `${base}${a.slice(baseVieja.length)}`])]
  for (const [de, a] of renombres) {
    if (de.toLowerCase() !== a.toLowerCase() && existsSync(join(absCarpetaVieja, a))) throw new ErrorAdmin(`Ya existe un archivo llamado «${a}» en la carpeta.`, 409)
  }
  const extras = db.prepare('SELECT id, ruta_relativa FROM extras WHERE media_item_id = ? AND substr(ruta_relativa, 1, ?) = ?').all(itemId, prefijo.length, prefijo) as { id: string; ruta_relativa: string }[]
  const plan: PlanHomologar = { actual: item.ruta_relativa, nuevo, acompanantes: acompanantes.length, extras: extras.length }
  if (!aplicar) return plan

  // Se anota cada renombrado para poder deshacerlos en orden inverso si uno falla.
  const hechos: [desde: string, hasta: string][] = []
  const mover = (desde: string, hasta: string): void => {
    renameSync(desde, hasta)
    hechos.push([desde, hasta])
  }
  try {
    // 1) archivos dentro de la carpeta vieja; 2) la carpeta.
    const nuevoArchivo = `${base}${ext}`
    if (archivoViejo !== nuevoArchivo) mover(join(absCarpetaVieja, archivoViejo), join(absCarpetaVieja, nuevoArchivo))
    for (const a of acompanantes) mover(join(absCarpetaVieja, a), join(absCarpetaVieja, `${base}${a.slice(baseVieja.length)}`))
    if (carpetaVieja !== base) mover(absCarpetaVieja, absCarpetaNueva)
    db.transaction(() => {
      db.prepare('UPDATE media_items SET ruta_relativa = ? WHERE id = ?').run(nuevo, itemId)
      for (const x of extras) db.prepare('UPDATE extras SET ruta_relativa = ? WHERE id = ?').run(`${base}/${x.ruta_relativa.slice(carpetaVieja.length + 1)}`, x.id)
    })()
  } catch (e) {
    for (const [desde, hasta] of hechos.reverse()) {
      try {
        renameSync(hasta, desde)
      } catch {
        /* mejor esfuerzo: se informa igual el error original */
      }
    }
    throw new ErrorAdmin(`No se pudo renombrar (${e instanceof Error ? e.message : 'error desconocido'}). No se cambió nada.`, 500)
  }
  return plan
}

// ------------------------------------------------------------------ usuarios y token de acceso

export interface UsuarioAdmin {
  id: string
  nombreUsuario: string
  rol: string
  creadoEn: string
  /** Conexiones con sesión vigente. */
  sesiones: number
}

const MIN_PASSWORD = 6

export function listarUsuarios(db: Database.Database): UsuarioAdmin[] {
  const filas = db
    .prepare('SELECT u.id, u.nombre_usuario, u.rol, u.creado_en, (SELECT COUNT(*) FROM sesiones s WHERE s.usuario_id = u.id AND s.expira_en > ?) AS sesiones FROM usuarios u ORDER BY lower(u.nombre_usuario)')
    .all(ahoraDb()) as { id: string; nombre_usuario: string; rol: string; creado_en: number; sesiones: number }[]
  return filas.map((f) => ({ id: f.id, nombreUsuario: f.nombre_usuario, rol: f.rol, creadoEn: new Date(f.creado_en * 1000).toISOString(), sesiones: f.sesiones }))
}

/** Usuarios y token tienen permisos de cliente (navegar, reproducir, progreso, listas): nunca administración. */
export async function crearUsuario(db: Database.Database, d: { nombreUsuario?: unknown; password?: unknown }): Promise<string> {
  const nombre = typeof d.nombreUsuario === 'string' ? d.nombreUsuario.trim() : ''
  if (nombre.length < 3 || nombre.length > 40) throw new ErrorAdmin('El nombre de usuario debe tener entre 3 y 40 caracteres.')
  if (typeof d.password !== 'string' || d.password.length < MIN_PASSWORD) throw new ErrorAdmin(`La contraseña debe tener al menos ${MIN_PASSWORD} caracteres.`)
  if (db.prepare('SELECT 1 FROM usuarios WHERE lower(nombre_usuario) = lower(?)').get(nombre)) throw new ErrorAdmin('Ya existe un usuario con ese nombre.', 409)
  const id = randomUUID()
  const hash = await bcrypt.hash(d.password, 10)
  db.prepare("INSERT INTO usuarios (id, nombre_usuario, password_hash, rol, creado_en) VALUES (?, ?, ?, 'cliente', ?)").run(id, nombre, hash, ahoraDb())
  return id
}

/** Cambiar la contraseña cierra todas las sesiones de ese usuario (quien la tenga abierta debe volver a entrar). */
export async function cambiarPassword(db: Database.Database, id: string, password: unknown): Promise<void> {
  if (typeof password !== 'string' || password.length < MIN_PASSWORD) throw new ErrorAdmin(`La contraseña debe tener al menos ${MIN_PASSWORD} caracteres.`)
  if (!db.prepare('SELECT 1 FROM usuarios WHERE id = ?').get(id)) throw new ErrorAdmin('El usuario no existe.', 404)
  const hash = await bcrypt.hash(password, 10)
  db.transaction(() => {
    db.prepare('UPDATE usuarios SET password_hash = ? WHERE id = ?').run(hash, id)
    db.prepare('DELETE FROM sesiones WHERE usuario_id = ?').run(id)
  })()
}

/** Borra el usuario y, en cascada, sus sesiones. Exige escribir su nombre exacto. */
export function eliminarUsuario(db: Database.Database, id: string, confirmar: unknown): void {
  const u = db.prepare('SELECT nombre_usuario FROM usuarios WHERE id = ?').get(id) as { nombre_usuario: string } | undefined
  if (!u) throw new ErrorAdmin('El usuario no existe.', 404)
  if (confirmar !== u.nombre_usuario) throw new ErrorAdmin('Escribe el nombre exacto del usuario para confirmar.', 409)
  db.transaction(() => {
    db.prepare('DELETE FROM sesiones WHERE usuario_id = ?').run(id)
    db.prepare('DELETE FROM usuarios WHERE id = ?').run(id)
  })()
}

/** Token de acceso (API key): 32 bytes aleatorios en hexadecimal, como en Flutter. Solo hay una activa. */
export function regenerarApiKey(db: Database.Database): string {
  const valor = randomBytes(32).toString('hex')
  db.transaction(() => {
    db.prepare('DELETE FROM api_keys').run()
    db.prepare('INSERT INTO api_keys (id, valor, activa, creado_en) VALUES (?, ?, 1, ?)').run(randomUUID(), valor, ahoraDb())
  })()
  return valor
}

// ------------------------------------------------------------------ archivo de un ítem

/** Dónde está el archivo de un ítem (solo administración: la API pública nunca expone rutas). */
export function infoArchivo(db: Database.Database, itemId: string): InfoArchivo {
  const f = db.prepare('SELECT id, library_id, ruta_relativa FROM media_items WHERE id = ?').get(itemId) as { id: string; library_id: string; ruta_relativa: string } | undefined
  if (!f) throw new ErrorAdmin('El ítem no existe.', 404)
  const lib = requerirBiblioteca(db, f.library_id)
  const abs = resolve(lib.ruta_actual, f.ruta_relativa)
  let existe = false
  let bytes: number | null = null
  try {
    const s = statSync(abs)
    existe = s.isFile()
    bytes = existe ? s.size : null
  } catch {
    /* no está */
  }
  return { bibliotecaId: lib.id, bibliotecaNombre: lib.nombre, carpetaBiblioteca: lib.ruta_actual, rutaRelativa: f.ruta_relativa, existe, bytes }
}
