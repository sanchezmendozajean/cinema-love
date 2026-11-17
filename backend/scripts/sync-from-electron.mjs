// Copia el backend de ../electron-version (src/main y src/shared) a backend/src, SIN modificar la versión Electron.
// Se excluye src/main/index.ts (ventana, IPC y demás cosas de Electron); app-core.ts se usa tal cual gracias al alias
// `electron` → src/electron-shim.ts del empaquetado. Volver a ejecutar para traer cambios del backend de referencia.
import { cpSync, rmSync, mkdirSync, existsSync, readdirSync, statSync } from 'node:fs'
import { join, dirname } from 'node:path'
import { fileURLToPath } from 'node:url'

const aqui = dirname(fileURLToPath(import.meta.url))
const origen = join(aqui, '..', '..', '..', 'electron-version', 'src')
const destino = join(aqui, '..', 'src', 'vendor')
if (!existsSync(origen)) throw new Error(`No se encuentra ${origen}`)

rmSync(destino, { recursive: true, force: true })
mkdirSync(destino, { recursive: true })
const filtro = (src) => !/\.(test|spec)\.ts$/.test(src) && !src.endsWith(join('main', 'index.ts'))
cpSync(join(origen, 'main'), join(destino, 'main'), { recursive: true, filter: filtro })
cpSync(join(origen, 'shared'), join(destino, 'shared'), { recursive: true, filter: filtro })

const contar = (d) => readdirSync(d).reduce((n, f) => { const p = join(d, f); return n + (statSync(p).isDirectory() ? contar(p) : 1) }, 0)
console.log(`Copiados ${contar(destino)} archivos de ${origen} → ${destino}`)
