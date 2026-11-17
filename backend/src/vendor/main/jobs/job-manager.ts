import { EventEmitter } from 'node:events'
import { randomUUID } from 'node:crypto'

export type TipoJob = 'escaneo' | 'metadata'
export type EstadoJob = 'corriendo' | 'completado' | 'error' | 'cancelado'

export interface Job {
  id: string
  tipo: TipoJob
  /** null = todas las bibliotecas. */
  bibliotecaId: string | null
  titulo: string
  estado: EstadoJob
  progreso: { hechos: number; total: number } | null
  /** Registro de texto (últimas `MAX_LINEAS`). */
  lineas: string[]
  resultado: unknown
  error: string | null
  creadoEn: number
  terminadoEn: number | null
}

/** Lo que recibe la función del trabajo para informar su avance y saber si lo cancelaron. */
export interface ContextoJob {
  senal: AbortSignal
  linea(texto: string): void
  progreso(hechos: number, total: number): void
}

export class JobEnCurso extends Error {
  constructor(readonly job: Job) {
    super('Ya hay un trabajo en curso para esta biblioteca.')
  }
}

const MAX_LINEAS = 2000
const MAX_HISTORIAL = 30

/**
 * Trabajos de larga duración (escaneos, importación de metadata). Un solo trabajo corriendo por biblioteca; los de
 * «todas las bibliotecas» (`bibliotecaId = null`) no conviven con ningún otro del mismo tipo.
 * Emite `cambio` (Job sin líneas) y `linea` ({ id, texto }) para el stream SSE.
 */
export class JobManager extends EventEmitter {
  private readonly jobs = new Map<string, { job: Job; control: AbortController }>()

  iniciar(spec: { tipo: TipoJob; bibliotecaId: string | null; titulo: string }, fn: (ctx: ContextoJob) => Promise<unknown>): Job {
    for (const { job } of this.jobs.values()) {
      if (job.estado !== 'corriendo') continue
      const choca = job.bibliotecaId === spec.bibliotecaId || (job.tipo === spec.tipo && (job.bibliotecaId === null || spec.bibliotecaId === null))
      if (choca) throw new JobEnCurso(job)
    }
    const control = new AbortController()
    const job: Job = { id: randomUUID(), ...spec, estado: 'corriendo', progreso: null, lineas: [], resultado: null, error: null, creadoEn: Date.now(), terminadoEn: null }
    this.jobs.set(job.id, { job, control })
    this.emitirCambio(job)

    const ctx: ContextoJob = {
      senal: control.signal,
      linea: (texto) => {
        job.lineas.push(texto)
        if (job.lineas.length > MAX_LINEAS) job.lineas.splice(0, job.lineas.length - MAX_LINEAS)
        this.emit('linea', { id: job.id, texto })
      },
      progreso: (hechos, total) => {
        job.progreso = { hechos, total }
        this.emitirCambio(job)
      }
    }
    void fn(ctx).then(
      (resultado) => this.terminar(job, control.signal.aborted ? 'cancelado' : 'completado', resultado, null),
      (e: unknown) => this.terminar(job, control.signal.aborted ? 'cancelado' : 'error', null, e instanceof Error ? e.message : String(e))
    )
    return job
  }

  private terminar(job: Job, estado: EstadoJob, resultado: unknown, error: string | null): void {
    job.estado = estado
    job.resultado = resultado
    job.error = error
    job.terminadoEn = Date.now()
    this.emitirCambio(job)
    // Solo se conservan los últimos trabajos terminados.
    const terminados = [...this.jobs.values()].filter((x) => x.job.estado !== 'corriendo').sort((a, b) => (a.job.terminadoEn ?? 0) - (b.job.terminadoEn ?? 0))
    for (const x of terminados.slice(0, Math.max(0, terminados.length - MAX_HISTORIAL))) this.jobs.delete(x.job.id)
  }

  private emitirCambio(job: Job): void {
    this.emit('cambio', this.resumen(job))
  }

  /** Copia sin el registro de texto (que viaja por `linea` o se pide aparte). */
  resumen(job: Job): Omit<Job, 'lineas'> {
    const { lineas: _lineas, ...resto } = job
    void _lineas
    return resto
  }

  obtener(id: string): Job | null {
    return this.jobs.get(id)?.job ?? null
  }

  listar(): Job[] {
    return [...this.jobs.values()].map((x) => x.job).sort((a, b) => b.creadoEn - a.creadoEn)
  }

  enCurso(): Job[] {
    return this.listar().filter((j) => j.estado === 'corriendo')
  }

  cancelar(id: string): boolean {
    const x = this.jobs.get(id)
    if (!x || x.job.estado !== 'corriendo') return false
    x.control.abort()
    return true
  }

  cancelarTodos(): void {
    for (const x of this.jobs.values()) if (x.job.estado === 'corriendo') x.control.abort()
  }
}
