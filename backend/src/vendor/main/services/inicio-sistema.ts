import { existsSync, mkdirSync, rmSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'

/** Lo que la interfaz necesita saber de «Iniciar con el sistema». */
export interface EstadoInicio {
  /** `false` en desarrollo (registraría el ejecutable de Electron) o en un sistema sin esta función. */
  disponible: boolean
  activo: boolean
  /** Por qué no está disponible, para explicarlo. */
  motivo: string | null
}

/** Dependencias del sistema, inyectables para probar el comportamiento de Linux sin tocar el equipo. */
export interface DepsInicio {
  plataforma: NodeJS.Platform
  /** Solo la app instalada/empaquetada se registra: en desarrollo sería el `electron.exe` suelto. */
  empaquetada: boolean
  /** Ejecutable a lanzar al iniciar sesión (en Linux, el AppImage si es lo que se está usando). */
  ejecutable: string
  /** Carpeta de configuración del usuario en Linux (`$XDG_CONFIG_HOME` o `~/.config`). */
  carpetaConfigLinux: string
  /** Windows/macOS: el sistema de elementos de inicio de sesión de Electron. */
  electron: { leer(): boolean; escribir(activo: boolean, ejecutable: string): void }
}

const ARCHIVO_LINUX = 'cinemalove.desktop'

/** Contenido del acceso de autoinicio de Linux (freedesktop). */
export function contenidoDesktop(ejecutable: string): string {
  const barra = String.fromCharCode(92)
  const citado = `"${ejecutable.split(barra).join(barra + barra).split('"').join(barra + '"')}"`
  return ['[Desktop Entry]', 'Type=Application', 'Name=CinemaLove', 'Comment=Tu catálogo de cine personal', `Exec=${citado}`, 'Terminal=false', 'X-GNOME-Autostart-enabled=true', ''].join(String.fromCharCode(10))
}

const rutaLinux = (d: DepsInicio): string => join(d.carpetaConfigLinux, 'autostart', ARCHIVO_LINUX)

export function leerInicio(d: DepsInicio): EstadoInicio {
  if (!d.empaquetada) return { disponible: false, activo: false, motivo: 'Solo está disponible en la aplicación instalada, no en modo de desarrollo.' }
  if (d.plataforma === 'linux') return { disponible: true, activo: existsSync(rutaLinux(d)), motivo: null }
  if (d.plataforma === 'win32' || d.plataforma === 'darwin') return { disponible: true, activo: d.electron.leer(), motivo: null }
  return { disponible: false, activo: false, motivo: 'Este sistema no admite iniciar con la sesión.' }
}

export function cambiarInicio(d: DepsInicio, activo: boolean): EstadoInicio {
  const actual = leerInicio(d)
  if (!actual.disponible) return actual
  if (d.plataforma === 'linux') {
    const ruta = rutaLinux(d)
    if (activo) {
      mkdirSync(join(d.carpetaConfigLinux, 'autostart'), { recursive: true })
      writeFileSync(ruta, contenidoDesktop(d.ejecutable), 'utf8')
    } else {
      rmSync(ruta, { force: true })
    }
  } else {
    d.electron.escribir(activo, d.ejecutable)
  }
  return leerInicio(d)
}
