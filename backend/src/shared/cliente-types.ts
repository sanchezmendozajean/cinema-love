/** Tipos del Modo Cliente compartidos main ↔ preload ↔ renderer (specs/03). */

export type TipoCredencial = 'sesion' | 'apiKey'

export type CodigoErrorCliente =
  | 'url-invalida'
  | 'sin-respuesta'
  | 'no-cinemalove'
  | 'certificado-no-confiable'
  | 'certificado-cambiado'
  | 'credenciales'
  | 'demasiados-intentos'
  | 'token-invalido'
  | 'sesion-vencida'
  | 'sin-conexion'
  | 'servidor-desconocido'

export interface InfoConexion {
  servidorId: string | null
  nombre: string
  url: string
  credencial: TipoCredencial
  usuario: string | null
  /** `true` si la conexión se recordó (se reconecta sola al abrir). */
  recordada: boolean
}

/** Estado de la conexión: la interfaz decide qué pantalla mostrar con esto. */
export type EstadoConexion =
  | { tipo: 'desconectada' }
  | { tipo: 'conectada'; info: InfoConexion; api: { baseUrl: string; token: string } }
  /** El servidor no responde (apagado, red caída). Se ofrece Reintentar / Olvidar, nunca caer a Local. */
  | { tipo: 'sin-conexion'; servidorId: string; nombre: string; url: string }
  | { tipo: 'sesion-vencida'; servidorId: string; nombre: string; usuario: string | null }
  /** El servidor presenta un certificado distinto al confiado: `huella` es la anterior y `huellaNueva` la que presenta ahora. */
  | { tipo: 'certificado-cambiado'; servidorId: string; nombre: string; url: string; huella: string; huellaNueva: string | null }

export type CredencialPeticion = { tipo: 'sesion'; usuario: string; password: string } | { tipo: 'apiKey'; apiKey: string }

export interface PeticionConexion {
  url: string
  credencial: CredencialPeticion
  /** Recordar el servidor y la credencial (token de sesión o API key; nunca la contraseña). */
  recordar: boolean
  /** Huella que la persona confirmó con «Confío en este certificado». */
  confiarHuella?: string
}

/** Resultado de «Verificar» la dirección de un servidor, listo para mostrarse. */
export type SondeoCliente =
  | { tipo: 'ok'; nombre: string; url: string; https: boolean }
  | { tipo: 'certificado-no-confiable' | 'certificado-cambiado'; nombre: string | null; url: string; huella: string }
  | { tipo: 'sin-respuesta' | 'no-cinemalove' | 'url-invalida' }

export type RespuestaConexion = { ok: true; estado: EstadoConexion } | { ok: false; codigo: CodigoErrorCliente; mensaje: string; huella?: string; nombreServidor?: string }

/** Servidor encontrado en la red por mDNS. */
export interface ServidorEnRed {
  id: string
  nombre: string
  host: string
  puerto: number
  tls: boolean
}

/** Lo que el asistente muestra de una carpeta de datos elegida. */
export interface ResumenCarpeta {
  existe: boolean
  /** Hay una colección (`cinemalove.sqlite`) en la carpeta. */
  coleccion: { bibliotecas: number; peliculas: number; series: number } | null
  /** ¿Se puede escribir ahí? */
  escribible: boolean
}
