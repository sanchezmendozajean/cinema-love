/** Valores idénticos a los textEnum de drift (camelCase). */
export const TIPOS_BIBLIOTECA = ['pelicula', 'serie'] as const
export type TipoBiblioteca = (typeof TIPOS_BIBLIOTECA)[number]

export const ESTADOS_ARCHIVO = ['ok', 'noEncontrado'] as const
export type EstadoArchivo = (typeof ESTADOS_ARCHIVO)[number]

export const ESTADOS_VISTO = ['noVisto', 'enProgreso', 'visto'] as const
export type EstadoVisto = (typeof ESTADOS_VISTO)[number]
