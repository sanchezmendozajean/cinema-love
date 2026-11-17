/** Canales IPC y tipos compartidos main ↔ preload ↔ renderer. */
import type { EstadoConexion, PeticionConexion, RespuestaConexion, ResumenCarpeta, ServidorEnRed, SondeoCliente } from './cliente-types'

export const IPC = {
  getBootstrap: 'app:get-bootstrap',
  windowMinimize: 'window:minimize',
  windowToggleMaximize: 'window:toggle-maximize',
  windowClose: 'window:close',
  windowState: 'window:state',
  windowSetFullScreen: 'window:set-fullscreen',
  windowFullScreenState: 'window:fullscreen-state',
  windowDragStart: 'window:drag-start',
  windowDragMove: 'window:drag-move',
  openExternal: 'app:open-external',
  abrirLogs: 'app:abrir-logs',
  abrirDatos: 'app:abrir-datos',
  cambiarCarpetaDatos: 'datos:cambiar-carpeta',
  inicioSistemaLeer: 'sistema:inicio-leer',
  inicioSistemaCambiar: 'sistema:inicio-cambiar',
  elegirRuta: 'app:elegir-ruta',
  clienteProbar: 'cliente:probar',
  clienteConectar: 'cliente:conectar',
  clienteReconectar: 'cliente:reconectar',
  clienteRenovarSesion: 'cliente:renovar-sesion',
  clienteConfiarNuevo: 'cliente:confiar-nuevo',
  clienteOlvidar: 'cliente:olvidar',
  clienteDesconectar: 'cliente:desconectar',
  clienteEstado: 'cliente:estado',
  clienteDescubrir: 'cliente:descubrir',
  clienteServidorEncontrado: 'cliente:servidor-encontrado',
  clienteServidorPerdido: 'cliente:servidor-perdido',
  bienvenidaInspeccionar: 'bienvenida:inspeccionar',
  bienvenidaCarpetaSugerida: 'bienvenida:carpeta-sugerida',
  bienvenidaAplicarCarpeta: 'bienvenida:aplicar-carpeta',
  bienvenidaDatosFlutter: 'bienvenida:datos-flutter'
} as const

export type ModoApp = 'local' | 'cliente'

export type EstadoDatos =
  | {
      ok: true
      soloLectura: boolean
      aviso: string | null
      bibliotecas: number
      peliculas: number
      series: number
      episodios: number
    }
  | { ok: false; motivo: 'sin-configurar' | 'version-antigua' | 'bloqueada' | 'error'; mensaje: string }

export interface Bootstrap {
  datos: EstadoDatos
  /** API activa: el listener interno (Modo Local) o el proxy hacia el servidor remoto (Modo Cliente). */
  api: { baseUrl: string; token: string } | null
  /** Estado de la conexión a otro CinemaLove (`desconectada` en Modo Local). */
  conexion: EstadoConexion
  modo: ModoApp
  /** «IP:puerto» si este equipo comparte su colección en la red (aunque esté viendo la de otro). */
  compartiendo: string | null
  plataforma: 'win32' | 'linux' | 'darwin'
  version: string
}

export interface CinemaLoveApi {
  getBootstrap(): Promise<Bootstrap>
  window: {
    minimize(): void
    toggleMaximize(): void
    close(): void
    onStateChange(cb: (maximizada: boolean) => void): () => void
    /** Pantalla completa real de la ventana (el reproductor la usa; no la HTML5 del <video>). */
    setFullScreen(activa: boolean): void
    onFullScreenChange(cb: (activa: boolean) => void): () => void
    /** Arrastre manual de la ventana (`dx`/`dy` desde donde empezó el gesto). */
    dragStart(): void
    dragMove(dx: number, dy: number): void
  }
  openExternal(url: string): void
  /** Abre la carpeta de registros en el explorador. `false` si no se pudo. */
  abrirLogs(): Promise<boolean>
  /** Abre la carpeta de datos de la colección local en el explorador. `false` si no se pudo. */
  abrirDatos(): Promise<boolean>
  /**
   * Pasa a usar otra carpeta de datos (Ajustes → Datos). No borra nada de la anterior; si la nueva no se puede abrir, se
   * restaura la anterior (`restaurada: true`) y se devuelve el error de la nueva.
   */
  cambiarCarpetaDatos(ruta: string): Promise<EstadoDatos & { restaurada?: boolean }>
  /** «Iniciar con el sistema» (solo en la app instalada). */
  inicioSistema: {
    leer(): Promise<{ disponible: boolean; activo: boolean; motivo: string | null }>
    cambiar(activo: boolean): Promise<{ disponible: boolean; activo: boolean; motivo: string | null }>
  }
  /** Conexión a otro CinemaLove (Modo Cliente). */
  cliente: {
    probar(url: string): Promise<SondeoCliente>
    /** `configurar`: dejar la conexión como Modo Cliente persistente (escribe config.yaml). Solo el asistente. */
    conectar(p: PeticionConexion, o?: { configurar?: boolean }): Promise<RespuestaConexion>
    reconectar(): Promise<EstadoConexion>
    renovarSesion(password: string): Promise<RespuestaConexion>
    confiarCertificadoNuevo(huella: string): Promise<RespuestaConexion>
    olvidar(): Promise<void>
    desconectar(): Promise<void>
    onEstado(cb: (e: EstadoConexion) => void): () => void
    /** Busca servidores por mDNS; devuelve cómo detener la búsqueda. */
    descubrir(alEncontrar: (s: ServidorEnRed) => void, alPerder: (id: string) => void): () => void
  }
  /** Asistente de bienvenida (primer arranque). */
  bienvenida: {
    inspeccionarCarpeta(ruta: string): Promise<ResumenCarpeta>
    carpetaSugerida(): Promise<string>
    /** Guarda config.yaml con esa carpeta de datos y abre la colección. */
    aplicarCarpeta(ruta: string): Promise<EstadoDatos>
    /** Lee `database_directory` de un config.yaml de la versión Flutter. */
    datosDeFlutter(rutaConfig: string): Promise<string | null>
  }
  /** Selector nativo de carpeta, archivo de video o `config.yaml`. `null` si se cancela. */
  elegirRuta(o: { tipo: 'carpeta' | 'video' | 'yaml'; titulo: string; rutaInicial?: string }): Promise<string | null>
}
