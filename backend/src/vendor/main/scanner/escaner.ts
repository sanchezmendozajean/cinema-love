import { createHash, randomUUID } from 'node:crypto'
import { promises as fs } from 'node:fs'
import { basename, dirname, extname, join, relative, resolve } from 'node:path'
import type Database from 'better-sqlite3'
import type { TipoBiblioteca } from '../../shared/enums'
import { ahoraDb } from '../db/codecs'
import { compararNatural, nombreSinExtension, numeroEpisodioExplicito, numeroTemporadaDeCarpeta, parsearTitulo } from './nombres'

/*
 * Port del `LibraryScannerService` de Flutter (specs/06 §2–4). Mismas reglas de carpetas, mismo hash de respaldo y
 * misma reconciliación: sobre los mismos archivos produce las mismas `ruta_relativa`, `hash_respaldo`, temporadas y
 * números de episodio que la versión anterior.
 */

export const EXTENSIONES_VIDEO = new Set(['.mp4', '.mkv', '.avi', '.mov', '.m4v', '.wmv', '.flv', '.webm', '.ts', '.mpg', '.mpeg'])

export interface Archivo {
  /** Ruta absoluta. */
  ruta: string
  /** Relativa a la raíz de la biblioteca, con `/` siempre. */
  relativa: string
  tamano: number
}

export interface BibliotecaEscaneable {
  id: string
  tipo: TipoBiblioteca
  rutaActual: string
}

export interface ResultadoEscaneo {
  nuevos: number
  reconciliados: number
  noEncontrados: number
  extrasNuevos: number
  extrasReconciliados: number
  seriesNuevas: number
  seriesReconciliadas: number
  seriesNoEncontradas: number
  /** Ids reconciliados por hash (se movieron o cambiaron de nombre): se muestran en la pantalla de reconciliación. */
  reconciliadosPorHashIds: string[]
}

export interface ProgresoEscaneo {
  fase: 'listando' | 'procesando' | 'cerrando'
  hechos: number
  total: number
  /** Ruta relativa del archivo que se está procesando. */
  actual?: string
}

export interface OpcionesEscaneo {
  senal?: AbortSignal
  onProgreso?: (p: ProgresoEscaneo) => void
  /** Inyectable para pruebas. */
  hash?: (ruta: string) => Promise<string>
}

export class EscaneoCancelado extends Error {
  constructor() {
    super('Escaneo cancelado')
  }
}

export class CarpetaNoDisponible extends Error {
  constructor(readonly ruta: string) {
    super(`La carpeta de la biblioteca no está disponible: ${ruta}`)
  }
}

// ------------------------------------------------------------------ sistema de archivos

/** Comparación de rutas como `p.equals` de Dart: normaliza y, en Windows, ignora mayúsculas. */
export function igualesRuta(a: string, b: string): boolean {
  const x = resolve(a)
  const y = resolve(b)
  return process.platform === 'win32' ? x.toLowerCase() === y.toLowerCase() : x === y
}

export const normalizarRelativa = (ruta: string): string => ruta.replace(/\\/g, '/')
const relativaA = (raiz: string, ruta: string): string => normalizarRelativa(relative(raiz, ruta))
export const esVideo = (ruta: string): boolean => EXTENSIONES_VIDEO.has(extname(ruta).toLowerCase())

/** Videos de un árbol, sin seguir enlaces simbólicos (como `followLinks: false`). */
export async function listarVideos(carpeta: string, raiz: string): Promise<Archivo[]> {
  const salida: Archivo[] = []
  const recorrer = async (dir: string): Promise<void> => {
    let entradas
    try {
      entradas = await fs.readdir(dir, { withFileTypes: true })
    } catch {
      return // carpeta ilegible: se ignora, no se aborta el escaneo entero
    }
    for (const e of entradas) {
      const p = join(dir, e.name)
      if (e.isDirectory()) await recorrer(p)
      else if (e.isFile() && esVideo(p)) {
        try {
          salida.push({ ruta: p, relativa: relativaA(raiz, p), tamano: (await fs.stat(p)).size })
        } catch {
          /* desapareció mientras se listaba */
        }
      }
    }
  }
  await recorrer(carpeta)
  return salida
}

const masPesado = (archivos: Archivo[]): Archivo => archivos.reduce((m, a) => (a.tamano > m.tamano ? a : m), archivos[0])

/**
 * Hash liviano de respaldo (specs/02 §5): SHA-256 de los primeros 64 KiB ‖ últimos 64 KiB (si el archivo supera 64 KiB)
 * ‖ UTF-8 del tamaño en bytes. Debe dar EXACTAMENTE el mismo valor que Flutter para que el relink funcione.
 * (La lectura es asíncrona y mueve 128 KiB por archivo: no bloquea el event loop, así que no hace falta un worker.)
 */
export async function hashDeRespaldo(ruta: string): Promise<string> {
  const BLOQUE = 64 * 1024
  const f = await fs.open(ruta, 'r')
  try {
    const { size } = await f.stat()
    const inicio = Buffer.alloc(Math.min(BLOQUE, size))
    await f.read(inicio, 0, inicio.length, 0)
    const partes = [inicio]
    if (size > BLOQUE) {
      const fin = Buffer.alloc(BLOQUE)
      await f.read(fin, 0, BLOQUE, size - BLOQUE)
      partes.push(fin)
    }
    partes.push(Buffer.from(String(size), 'utf8'))
    return createHash('sha256').update(Buffer.concat(partes)).digest('hex')
  } finally {
    await f.close()
  }
}

// ------------------------------------------------------------------ plan (solo lectura del disco)

export interface PlanPelicula {
  tipo: 'pelicula'
  carpeta: string
  oficial: Archivo
  extras: Archivo[]
}

export interface EpisodioPlan {
  archivo: Archivo
  temporada: number
  episodio: number
}

export interface PlanSerie {
  tipo: 'serie'
  /** Ruta relativa de la carpeta de la serie. */
  carpeta: string
  episodios: EpisodioPlan[]
}

export type Plan = PlanPelicula | PlanSerie

/** Temporada de un archivo: carpeta «Season N»/«Temporada N» más cercana hacia arriba; si no, `SxxEyy`; si no, 1. */
export function temporadaDeArchivo(archivo: string, raizSerie: string): number {
  let carpeta = dirname(archivo)
  while (!igualesRuta(carpeta, raizSerie)) {
    const n = numeroTemporadaDeCarpeta(basename(carpeta))
    if (n != null) return n
    const padre = dirname(carpeta)
    if (igualesRuta(padre, carpeta)) break // raíz del filesystem
    carpeta = padre
  }
  return numeroEpisodioExplicito(basename(archivo))?.temporada ?? 1
}

/**
 * Numera los episodios de un grupo (serie, temporada): si TODOS traen número explícito y no hay duplicados se usan
 * esos; si no, se descartan y se numeran 1..N por orden natural del nombre de archivo.
 */
export function numerarEpisodios(archivos: Archivo[]): Map<Archivo, number> {
  const explicitos = new Map<Archivo, number>()
  let completo = true
  for (const a of archivos) {
    const m = numeroEpisodioExplicito(basename(a.ruta))
    if (!m) {
      completo = false
      break
    }
    explicitos.set(a, m.episodio)
  }
  if (completo && new Set(explicitos.values()).size === explicitos.size) return explicitos
  const ordenados = [...archivos].sort((a, b) => compararNatural(basename(a.ruta), basename(b.ruta)))
  return new Map(ordenados.map((a, i) => [a, i + 1]))
}

/**
 * Lee el disco y arma qué hay en la biblioteca, sin tocar la base: una carpeta por película (primer nivel) o por
 * serie. Es la parte que se compara con los datos de Flutter para verificar la paridad.
 */
export async function planificarBiblioteca(biblioteca: BibliotecaEscaneable, senal?: AbortSignal, onListado?: (hechas: number, total: number) => void): Promise<Plan[]> {
  const raiz = biblioteca.rutaActual
  const carpetas = (await fs.readdir(raiz, { withFileTypes: true })).filter((e) => e.isDirectory())
  const planes: Plan[] = []
  let hechas = 0
  for (const c of carpetas) {
    if (senal?.aborted) throw new EscaneoCancelado()
    const carpeta = join(raiz, c.name)
    const videos = await listarVideos(carpeta, raiz)
    onListado?.(++hechas, carpetas.length)
    if (videos.length === 0) continue

    if (biblioteca.tipo === 'serie') {
      const porTemporada = new Map<number, Archivo[]>()
      for (const v of videos) {
        const t = temporadaDeArchivo(v.ruta, carpeta)
        porTemporada.set(t, [...(porTemporada.get(t) ?? []), v])
      }
      const episodios: EpisodioPlan[] = []
      for (const [temporada, grupo] of porTemporada) {
        for (const [archivo, episodio] of numerarEpisodios(grupo)) episodios.push({ archivo, temporada, episodio })
      }
      planes.push({ tipo: 'serie', carpeta: relativaA(raiz, carpeta), episodios })
    } else {
      const directos = videos.filter((v) => igualesRuta(dirname(v.ruta), carpeta))
      // Sin video directo en la carpeta (estructura atípica) se recurre a todo el árbol para no perder el contenido.
      const oficial = masPesado(directos.length ? directos : videos)
      planes.push({ tipo: 'pelicula', carpeta: relativaA(raiz, carpeta), oficial, extras: videos.filter((v) => v.ruta !== oficial.ruta) })
    }
  }
  return planes
}

// ------------------------------------------------------------------ reconciliación (escribe la base)

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type Fila = Record<string, any>

/** Limita la cantidad de operaciones de disco simultáneas. */
async function enParalelo<T, R>(items: T[], limite: number, fn: (x: T) => Promise<R>): Promise<R[]> {
  const out: R[] = new Array(items.length)
  let i = 0
  const trabajador = async (): Promise<void> => {
    while (i < items.length) {
      const k = i++
      out[k] = await fn(items[k])
    }
  }
  await Promise.all(Array.from({ length: Math.min(limite, items.length) }, trabajador))
  return out
}

/**
 * Escanea una biblioteca y la reconcilia con la base (specs/06 §4):
 * por ruta relativa → por hash de respaldo (se movió/renombró) → ítem nuevo; lo que no aparece queda como
 * `noEncontrado` y NUNCA se borra solo. Si el escaneo se cancela, no se marca nada como ausente.
 */
export async function escanearBiblioteca(db: Database.Database, biblioteca: BibliotecaEscaneable, opciones: OpcionesEscaneo = {}): Promise<ResultadoEscaneo> {
  const hash = opciones.hash ?? hashDeRespaldo
  const progreso = opciones.onProgreso ?? ((): void => undefined)
  const senal = opciones.senal

  try {
    await fs.access(biblioteca.rutaActual)
  } catch {
    // Con la carpeta inalcanzable (disco de red apagado) marcar todo como «no encontrado» ocultaría la colección
    // entera del Catálogo por un problema pasajero. Flutter lo hacía; aquí se avisa y no se toca nada.
    throw new CarpetaNoDisponible(biblioteca.rutaActual)
  }

  progreso({ fase: 'listando', hechos: 0, total: 0 })
  const planes = await planificarBiblioteca(biblioteca, senal, (h, t) => progreso({ fase: 'listando', hechos: h, total: t }))

  const total = planes.reduce((n, p) => n + (p.tipo === 'serie' ? p.episodios.length : 1 + p.extras.length), 0)
  let hechos = 0
  const avance = (rel: string): void => {
    progreso({ fase: 'procesando', hechos: ++hechos, total, actual: rel })
    if (senal?.aborted) throw new EscaneoCancelado()
  }

  const r: ResultadoEscaneo = {
    nuevos: 0,
    reconciliados: 0,
    noEncontrados: 0,
    extrasNuevos: 0,
    extrasReconciliados: 0,
    seriesNuevas: 0,
    seriesReconciliadas: 0,
    seriesNoEncontradas: 0,
    reconciliadosPorHashIds: []
  }

  const libId = biblioteca.id
  const existentes = db.prepare('SELECT * FROM media_items WHERE library_id = ?').all(libId) as Fila[]
  const extrasExistentes = db
    .prepare('SELECT e.* FROM extras e JOIN media_items m ON m.id = e.media_item_id WHERE m.library_id = ?')
    .all(libId) as Fila[]
  const seriesExistentes = db.prepare('SELECT * FROM series WHERE library_id = ?').all(libId) as Fila[]
  const vistos = new Set<string>()
  const extrasVistos = new Set<string>()
  const seriesVistas = new Set<string>()

  // El hash solo se calcula para lo que no casa por ruta (nuevo o movido): como en Flutter. Para no esperar archivo por
  // archivo se precalcula en paralelo lo que seguro lo necesitará.
  const rutasConocidas = new Set(existentes.map((m) => m.ruta_relativa as string))
  const rutasExtraConocidas = new Set(extrasExistentes.map((e) => `${e.media_item_id}|${e.ruta_relativa}`))
  const hashes = new Map<string, Promise<string>>()
  const hashDe = (a: Archivo): Promise<string> => {
    let h = hashes.get(a.ruta)
    if (!h) hashes.set(a.ruta, (h = hash(a.ruta)))
    return h
  }
  const precalcular = async (archivos: Archivo[]): Promise<void> => {
    await enParalelo(archivos, 8, async (a) => {
      if (senal?.aborted) return
      await hashDe(a).catch(() => undefined)
    })
  }
  const aHashear = planes.flatMap((p) =>
    p.tipo === 'serie' ? p.episodios.map((e) => e.archivo) : [p.oficial, ...p.extras]
  ).filter((a) => !rutasConocidas.has(a.relativa) && ![...rutasExtraConocidas].some((k) => k.endsWith(`|${a.relativa}`)))
  await precalcular(aHashear)

  const ahora = ahoraDb()
  const buscarPorRuta = db.prepare('SELECT id FROM media_items WHERE library_id = ? AND ruta_relativa = ?')

  const reconciliarItem = async (a: Archivo, extra: { seriesId: string | null; temporada: number | null; episodio: number | null }): Promise<{ nuevo: boolean; idPorHash: string | null }> => {
    const existente = existentes.find((m) => m.ruta_relativa === a.relativa)
    if (existente) {
      vistos.add(existente.id)
      const cambioEstado = existente.estado_archivo !== 'ok'
      const cambioSerie = existente.series_id !== extra.seriesId || existente.numero_temporada !== extra.temporada || existente.numero_episodio !== extra.episodio
      if (cambioEstado || cambioSerie) {
        db.prepare("UPDATE media_items SET estado_archivo = 'ok', series_id = ?, numero_temporada = ?, numero_episodio = ?, actualizado_en = ? WHERE id = ?").run(
          extra.seriesId,
          extra.temporada,
          extra.episodio,
          ahora,
          existente.id
        )
      }
      return { nuevo: false, idPorHash: null }
    }

    const h = await hashDe(a)
    const porHash = existentes.find((c) => !vistos.has(c.id) && c.hash_respaldo != null && c.hash_respaldo === h)
    if (porHash) {
      vistos.add(porHash.id)
      db.prepare(
        "UPDATE media_items SET ruta_relativa = ?, estado_archivo = 'ok', hash_respaldo = ?, series_id = ?, numero_temporada = ?, numero_episodio = ?, actualizado_en = ? WHERE id = ?"
      ).run(a.relativa, h, extra.seriesId, extra.temporada, extra.episodio, ahora, porHash.id)
      return { nuevo: false, idPorHash: porHash.id }
    }

    const id = randomUUID()
    db.prepare(
      "INSERT INTO media_items (id, library_id, ruta_relativa, estado_archivo, hash_respaldo, series_id, numero_temporada, numero_episodio, creado_en, actualizado_en) VALUES (?, ?, ?, 'ok', ?, ?, ?, ?, ?, ?)"
    ).run(id, libId, a.relativa, h, extra.seriesId, extra.temporada, extra.episodio, ahora, ahora)
    vistos.add(id)
    return { nuevo: true, idPorHash: null }
  }

  const reconciliarExtra = async (mediaItemId: string, a: Archivo): Promise<boolean> => {
    const existente = extrasExistentes.find((e) => e.media_item_id === mediaItemId && e.ruta_relativa === a.relativa)
    if (existente) {
      extrasVistos.add(existente.id)
      if (existente.estado_archivo !== 'ok') db.prepare("UPDATE extras SET estado_archivo = 'ok' WHERE id = ?").run(existente.id)
      return false
    }
    const h = await hashDe(a)
    const porHash = extrasExistentes.find((c) => c.media_item_id === mediaItemId && !extrasVistos.has(c.id) && c.hash_respaldo != null && c.hash_respaldo === h)
    if (porHash) {
      extrasVistos.add(porHash.id)
      db.prepare("UPDATE extras SET ruta_relativa = ?, estado_archivo = 'ok', hash_respaldo = ? WHERE id = ?").run(a.relativa, h, porHash.id)
      return false
    }
    // Título derivado SOLO del nombre de archivo: si viniera de una carpeta «extras» todos se llamarían «Extras».
    const titulo = parsearTitulo(basename(a.relativa)).titulo
    const id = randomUUID()
    db.prepare("INSERT INTO extras (id, media_item_id, ruta_relativa, hash_respaldo, estado_archivo, titulo, creado_en) VALUES (?, ?, ?, ?, 'ok', ?, ?)").run(id, mediaItemId, a.relativa, h, titulo, ahora)
    extrasVistos.add(id)
    return true
  }

  for (const plan of planes) {
    if (plan.tipo === 'pelicula') {
      const res = await reconciliarItem(plan.oficial, { seriesId: null, temporada: null, episodio: null })
      if (res.nuevo) r.nuevos++
      else {
        r.reconciliados++
        if (res.idPorHash) r.reconciliadosPorHashIds.push(res.idPorHash)
      }
      avance(plan.oficial.relativa)
      const oficialId = (buscarPorRuta.get(libId, plan.oficial.relativa) as Fila | undefined)?.id as string | undefined
      for (const x of plan.extras) {
        if (oficialId) {
          if (await reconciliarExtra(oficialId, x)) r.extrasNuevos++
          else r.extrasReconciliados++
        }
        avance(x.relativa)
      }
    } else {
      const serieExistente = seriesExistentes.find((s) => s.ruta_relativa === plan.carpeta)
      let serieId: string
      if (serieExistente) {
        serieId = serieExistente.id
        seriesVistas.add(serieId)
        if (serieExistente.estado_archivo !== 'ok') db.prepare("UPDATE series SET estado_archivo = 'ok', actualizado_en = ? WHERE id = ?").run(ahora, serieId)
        r.seriesReconciliadas++
      } else {
        serieId = randomUUID()
        db.prepare("INSERT INTO series (id, library_id, ruta_relativa, estado_archivo, creado_en, actualizado_en) VALUES (?, ?, ?, 'ok', ?, ?)").run(serieId, libId, plan.carpeta, ahora, ahora)
        seriesVistas.add(serieId)
        r.seriesNuevas++
      }
      for (const ep of plan.episodios) {
        const res = await reconciliarItem(ep.archivo, { seriesId: serieId, temporada: ep.temporada, episodio: ep.episodio })
        if (res.nuevo) r.nuevos++
        else {
          r.reconciliados++
          if (res.idPorHash) r.reconciliadosPorHashIds.push(res.idPorHash)
        }
        avance(ep.archivo.relativa)
      }
    }
  }

  // Solo si el escaneo llegó al final: lo que no se vio queda «no encontrado» (nunca se borra solo).
  progreso({ fase: 'cerrando', hechos: total, total })
  const marcar = db.prepare("UPDATE media_items SET estado_archivo = 'noEncontrado', actualizado_en = ? WHERE id = ?")
  for (const m of existentes) {
    if (vistos.has(m.id)) continue
    if (m.estado_archivo !== 'noEncontrado') marcar.run(ahora, m.id)
    r.noEncontrados++
  }
  if (biblioteca.tipo === 'serie') {
    for (const s of seriesExistentes) {
      if (seriesVistas.has(s.id)) continue
      if (s.estado_archivo !== 'noEncontrado') db.prepare("UPDATE series SET estado_archivo = 'noEncontrado', actualizado_en = ? WHERE id = ?").run(ahora, s.id)
      r.seriesNoEncontradas++
    }
  } else {
    for (const e of extrasExistentes) {
      if (extrasVistos.has(e.id)) continue
      if (e.estado_archivo !== 'noEncontrado') db.prepare("UPDATE extras SET estado_archivo = 'noEncontrado' WHERE id = ?").run(e.id)
    }
  }
  return r
}

export { nombreSinExtension }
