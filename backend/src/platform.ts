// Entorno de ejecución del backend (rutas y modo). Lo configura la app Qt mediante variables de entorno (ver BackendLauncher).
import { join } from 'node:path'
import { homedir } from 'node:os'

const raiz = process.env['CL_APP_ROOT'] ?? process.cwd()

export const app = {
  // Siempre «desarrollo»: las rutas de datos y ffmpeg las fijan las variables de entorno que pone el lanzador de Qt.
  isPackaged: false,
  getAppPath: (): string => raiz,
  getVersion: (): string => process.env['CL_VERSION'] ?? '0.1.0',
  getPath: (nombre: string): string => {
    if (nombre === 'home') return homedir()
    return process.env['CL_USER_DATA'] ?? join(raiz, '.dev-data')
  }
}
