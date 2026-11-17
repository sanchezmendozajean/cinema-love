// Entrada del sidecar: levanta AppCore (colección local + listener interno) sin ventana y avisa por stdout cuando está listo.
// Protocolo con la app Qt (specs/03 §2): una línea `CL_READY {"baseUrl":…,"token":…}` o `CL_ERROR {…}`; se cierra al recibir
// SIGTERM, al cerrarse stdin (la app Qt murió) o con la orden «quit» por stdin.
import { AppCore } from './main/app-core'
import { iniciarControl } from './main/client/control'

const core = new AppCore()
let cerrando = false

async function cerrar(codigo: number): Promise<void> {
  if (cerrando) return
  cerrando = true
  try {
    await core.cerrar()
  } catch (e) {
    console.error('Error al cerrar:', e instanceof Error ? e.message : e)
  }
  process.exit(codigo)
}

process.on('SIGTERM', () => void cerrar(0))
process.on('SIGINT', () => void cerrar(0))
process.on('uncaughtException', (e) => { console.error('Excepción no controlada:', e); void cerrar(1) })
process.stdin.on('end', () => void cerrar(0)) // el proceso padre desapareció
process.stdin.on('data', (b) => { if (b.toString().includes('quit')) void cerrar(0) })
process.stdin.resume()

/** Modo Cliente (versión Qt): sin colección local; la app gestiona la conexión por el servidor de control (CL_CLIENTE_DIR = carpeta de cliente_datos.sqlite). */
async function mainCliente(carpeta: string): Promise<void> {
  core.usarCarpetaCliente(carpeta)
  const control = await iniciarControl(core, carpeta)
  const cerrarTodo = core.cerrar.bind(core)
  core.cerrar = async () => { await control.cerrar(); await cerrarTodo() }
  console.log('CL_READY ' + JSON.stringify({ baseUrl: control.baseUrl, token: control.token, modo: 'cliente' }))
}

async function main(): Promise<void> {
  if (process.env.CL_CLIENTE_DIR) return mainCliente(process.env.CL_CLIENTE_DIR)
  await core.iniciar()
  const datos = core.estadoDatos
  const api = core.api
  if (!datos.ok || !api) {
    console.log('CL_ERROR ' + JSON.stringify({ datos }))
    await cerrar(2)
    return
  }
  console.log('CL_READY ' + JSON.stringify({ baseUrl: api.baseUrl, token: api.token, datos }))
}

void main().catch((e: unknown) => {
  console.log('CL_ERROR ' + JSON.stringify({ mensaje: e instanceof Error ? e.message : String(e) }))
  void cerrar(2)
})
