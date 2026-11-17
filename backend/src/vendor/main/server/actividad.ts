/** Una reproducción «direct» en curso, deducida de las peticiones Range que el cliente va haciendo. */
export interface ActividadDirecta {
  quien: string
  titulo: string
  cliente: string
  desde: number
  ultimaVez: number
}

/**
 * Reproducciones directas recientes. El «direct» no abre sesión (es un archivo servido con Range), así que se considera
 * activa mientras siga pidiendo bytes. Se comparte entre el listener interno y el público para el panel «Reproduciendo ahora».
 */
export class RegistroActividad {
  private readonly mapa = new Map<string, ActividadDirecta>()

  marcar(clave: string, datos: { quien: string; titulo: string; cliente: string }): void {
    const ahora = Date.now()
    const previa = this.mapa.get(clave)
    this.mapa.set(clave, { ...datos, desde: previa?.desde ?? ahora, ultimaVez: ahora })
  }

  activas(ventanaMs = 20_000): ActividadDirecta[] {
    const limite = Date.now() - ventanaMs
    for (const [k, v] of this.mapa) if (v.ultimaVez < limite) this.mapa.delete(k)
    return [...this.mapa.values()].sort((a, b) => a.desde - b.desde)
  }
}
