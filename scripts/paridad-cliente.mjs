// Paridad del Modo Cliente integrado (C++) con el del backend Node: ambos se conectan a un MISMO servidor (el backend C++ con el servidor público
// activo) y se comparan control, espejo del catálogo, proxy (token y usuario), reconexión, sesión vencida y servidor caído.
// Uso: third_party/node/node.exe scripts/paridad-cliente.mjs   (requiere build/alt|win compilado y .dev-data/cinemalove.sqlite)
import { spawn } from 'node:child_process'
import { copyFileSync, mkdirSync, rmSync, existsSync, statSync, readdirSync } from 'node:fs'
import { createRequire } from 'node:module'
import { fileURLToPath } from 'node:url'
import { dirname, resolve } from 'node:path'
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..').split(String.fromCharCode(92)).join('/')
const exe = [root + '/build/win/Release/cinemalove.exe', root + '/build/alt/Release/cinemalove.exe'].filter(existsSync).sort((a, b) => statSync(b).mtimeMs - statSync(a).mtimeMs)[0]
const S = root + '/.dev-data/paridad-cli'
rmSync(S, { recursive: true, force: true }); for (const d of ['rem', 'cli-node', 'cli-qt']) mkdirSync(S + '/' + d, { recursive: true })
copyFileSync(root + '/.dev-data/cinemalove.sqlite', S + '/rem/cinemalove.sqlite')
const Sqlite = createRequire(root + '/backend/package.json')('better-sqlite3')
const PATH = 'D:/Qt/6.8.3/msvc2022_64/bin;' + process.env.PATH
const lanzar = (cmd, args, env) => new Promise((res, rej) => {
  const p = spawn(cmd, args, { env: { ...process.env, PATH, ...env }, stdio: ['pipe', 'pipe', 'inherit'] })
  let b = ''; p.stdout.on('data', d => { b += d; const m = /CL_(READY|ERROR) (.*)/.exec(b); if (m) m[1] === 'READY' ? res({ p, ...JSON.parse(m[2]) }) : rej(new Error(m[2])) })
})
const J = async (o, m, path, body, tok = o.token, base = o.baseUrl) => { const r = await fetch(base + path, { method: m, headers: body !== undefined ? { 'X-Api-Key': tok, 'Content-Type': 'application/json' } : { 'X-Api-Key': tok }, body: body !== undefined ? JSON.stringify(body) : undefined }); return { s: r.status, j: await r.json().catch(() => null) } }
const sort = (x) => Array.isArray(x) ? x.map(sort) : x && typeof x === 'object' ? Object.fromEntries(Object.keys(x).sort().map(k => [k, sort(x[k])])) : x
process.on('uncaughtException', (e) => { console.log('EXCEPCION', e.stack, e.cause); process.exit(2) })
let ok = 0, fail = 0
const same = (etiqueta, a, b) => { const x = typeof a === 'string' ? a : JSON.stringify(sort(a)), y = typeof b === 'string' ? b : JSON.stringify(sort(b)); if (x === y) ok++; else { fail++; let k = 0; while (k < x.length && x[k] === y[k]) k++; console.log('DIFERENCIA', etiqueta, '\n  node:', x.slice(Math.max(0, k - 100), k + 220), '\n  qt  :', y.slice(Math.max(0, k - 100), k + 220)) } }
const esperar = async (cond, ms = 40000) => { for (let i = 0; i < ms / 200; i++) { if (await cond()) return true; await new Promise(r => setTimeout(r, 200)) } return false }

// ---- servidor remoto (C++) con el servidor público
const rem = await lanzar(exe, ['--serve', S + '/rem'])
await J(rem, 'PUT', '/api/v1/admin/servidor/config', { puertoHttp: 8097, protocolo: 'http', nombre: 'Sala de pruebas' })
await J(rem, 'POST', '/api/v1/admin/servidor/iniciar')
const key = (await J(rem, 'GET', '/api/v1/admin/api-key')).j.valor
for (const u of ['ana', 'beto']) await J(rem, 'POST', '/api/v1/admin/usuarios', { nombreUsuario: u, password: 'secreta1' })
const URLR = 'http://127.0.0.1:8097'

const cn = await lanzar(root + '/third_party/node/node.exe', [root + '/backend/dist/main.cjs'], { CL_APP_ROOT: root, CL_SIN_FFMPEG: '1', CL_CLIENTE_DIR: S + '/cli-node' })
const cq = await lanzar(exe, ['--client-serve', S + '/cli-qt'])
const pair = async (etiqueta, fn) => { const a = await fn(cn, 'node'), b = await fn(cq, 'qt'); same(etiqueta, a, b); return [a, b] }
const limpiarEstado = (e) => { const x = JSON.parse(JSON.stringify(e)); if (x.info) delete x.info.servidorId; delete x.servidorId; if (x.api) x.api = 'api'; const q = JSON.stringify(x).split('"beto"').join('"ana"'); return JSON.parse(q) }

// ---- verificación
if (process.env.DBG) { const sid = (await J(rem, 'GET', '/api/v1/libraries')).j.find(l => l.tipo === 'serie').id; const ss = (await J(rem, 'GET', '/api/v1/libraries/' + sid + '/series')).j; await J(cq, 'POST', '/control/conectar', { url: URLR, credencial: { tipo: 'apiKey', apiKey: key }, recordar: true }); await new Promise(r => setTimeout(r, 4000)); const e = (await J(cq, 'GET', '/control/estado')).j; console.log(e.espejo, e.conexion.api); const r = await J(e.conexion.api, 'GET', '/api/v1/series/' + ss[0].id, undefined, e.conexion.api.token, e.conexion.api.baseUrl); console.log(r.s, JSON.stringify(r.j).slice(0, 300)); const d = await J(rem, 'GET', '/api/v1/series/' + ss[0].id); console.log(d.s, JSON.stringify(d.j).slice(0, 300)); process.exit(0) }
await pair('probar ok', async (o) => (await J(o, 'POST', '/control/probar', { url: URLR })).j)
await pair('probar sin esquema', async (o) => (await J(o, 'POST', '/control/probar', { url: '127.0.0.1:8097' })).j)
await pair('probar invalida', async (o) => (await J(o, 'POST', '/control/probar', { url: '://x' })).j)
await pair('probar sin respuesta', async (o) => (await J(o, 'POST', '/control/probar', { url: 'http://127.0.0.1:9' })).j)
await pair('probar no es cinemalove', async (o) => (await J(o, 'POST', '/control/probar', { url: 'http://127.0.0.1:' + new URL(o.baseUrl).port })).j)

// ---- conexión con token
await pair('conectar token malo', async (o) => (await J(o, 'POST', '/control/conectar', { url: URLR, credencial: { tipo: 'apiKey', apiKey: 'mala' }, recordar: true })).j)
const con = {}
await pair('conectar token', async (o, n) => { const r = (await J(o, 'POST', '/control/conectar', { url: URLR, credencial: { tipo: 'apiKey', apiKey: key }, recordar: true })).j; con[n] = r.estado; return { ok: r.ok, estado: limpiarEstado(r.estado) } })
const listo = async (o) => (await J(o, 'GET', '/control/estado')).j.espejo.fase === 'listo'
for (const o of [cn, cq]) if (!(await esperar(() => listo(o)))) console.log('el espejo no terminó')
const est = {}; for (const [n, o] of [['node', cn], ['qt', cq]]) est[n] = (await J(o, 'GET', '/control/estado')).j
same('estado tras conectar', { c: limpiarEstado(est.node.conexion), e: { ...est.node.espejo, ruta: 0 }, r: est.node.recordados.map(x => ({ n: x.nombre, u: x.url })) }, { c: limpiarEstado(est.qt.conexion), e: { ...est.qt.espejo, ruta: 0 }, r: est.qt.recordados.map(x => ({ n: x.nombre, u: x.url })) })

// ---- espejo: mismo contenido (se ignoran rutas, que llevan el puerto y el token del proxy)
const volcado = (ruta) => { const db = new Sqlite(ruta, { readonly: true }); const out = {}
  for (const t of ['libraries', 'media_items', 'series', 'seasons', 'watch_progresses', 'proximas_a_ver']) { out[t] = db.prepare(`SELECT * FROM ${t} ORDER BY ${t === 'watch_progresses' || t === 'proximas_a_ver' ? 'media_item_id' : 'id'}`).all().map(r => { delete r.ruta_actual; delete r.ruta_relativa; delete r.actualizado_en; delete r.creado_en; delete r.id_; if (t === 'watch_progresses' || t === 'proximas_a_ver') { delete r.id; delete r.ultima_reproduccion; delete r.agregado_en } return r }) }
  db.close(); return out }
const vn = volcado(est.node.espejo.ruta), vq = volcado(est.qt.espejo.ruta)
for (const t of Object.keys(vn)) same('espejo ' + t + ' (' + vn[t].length + ')', JSON.stringify(vn[t]), JSON.stringify(vq[t]))

// ---- proxy en modo token (lo personal vive en este equipo)
const api = (n) => ({ baseUrl: (n === 'node' ? con.node : con.qt).api.baseUrl, token: (n === 'node' ? con.node : con.qt).api.token })
const { j: lib0 } = await J(rem, 'GET', '/api/v1/libraries')
const pelis = (await J(rem, 'GET', `/api/v1/libraries/${lib0.find(l => l.tipo === 'pelicula').id}/items`)).j
const series = (await J(rem, 'GET', `/api/v1/libraries/${lib0.find(l => l.tipo === 'serie').id}/series`)).j
const [a, b, c] = pelis.slice(5, 8).map(m => m.id)
const secuencia = async (o, n) => {
  const ap = api(n); const out = []; const ids = new Map()
  const reg = async (t, m, path, body) => { const r = await J(ap, m, path, body, ap.token, ap.baseUrl); if (r.j && r.j.id && path === '/api/v1/collections' && m === 'POST' && r.s === 200) { ids.set(r.j.id, 'COL1') }; let s = JSON.stringify(sort(r.j), (k, v) => (k === 'creadoEn' ? undefined : v)) ?? 'null'; for (const [real, ph] of ids) s = s.split(real).join(ph); out.push(`${t} ${m} ${path.replace(/[0-9a-f]{8}-[0-9a-f-]{27}/g, (x) => ids.get(x) ?? x)} -> ${r.s} ${s}`); return r.j }
  await reg('', 'GET', '/api/v1/health'); await reg('', 'GET', '/api/v1/me')
  await reg('', 'PUT', `/api/v1/media/${a}/progress`, { posicionSegundos: 1234, pistaAudioId: '2', pistaSubtituloId: '3' }); await reg('', 'GET', `/api/v1/media/${a}/progress`)
  await reg('', 'PUT', `/api/v1/media/${b}/progress`, { estado: 'visto' }); await reg('', 'PUT', `/api/v1/media/${c}/progress`, { estado: 'raro' }); await reg('', 'GET', `/api/v1/media/${a}/user-state`)
  await reg('', 'PUT', `/api/v1/watchlist/${a}`); await new Promise(r => setTimeout(r, 1100)); await reg('', 'PUT', `/api/v1/watchlist/${c}`); await reg('', 'GET', '/api/v1/watchlist'); await reg('', 'GET', '/api/v1/watchlist?formato=catalogo'); await reg('', 'DELETE', `/api/v1/watchlist/${c}`)
  const col = await reg('', 'POST', '/api/v1/collections', { nombre: ' Mi lista ', quitarAlVerse: true }); ids.set(col.id, 'COL1')
  await reg('', 'POST', '/api/v1/collections', {}); await reg('', 'POST', `/api/v1/collections/${col.id}/items`, { mediaItemId: a }); await new Promise(r => setTimeout(r, 1100)); await reg('', 'POST', `/api/v1/collections/${col.id}/items`, { mediaItemId: b })
  await reg('', 'GET', '/api/v1/collections'); await reg('', 'GET', `/api/v1/collections/${col.id}`); await reg('', 'GET', `/api/v1/collections/${col.id}/items`); await reg('', 'GET', `/api/v1/collections/${col.id}/items?formato=catalogo`)
  await reg('', 'GET', `/api/v1/collections/item/${a}`); await reg('', 'PUT', `/api/v1/collections/${col.id}/progress/${a}`, { posicionSegundos: 77 }); await reg('', 'GET', `/api/v1/collections/${col.id}/continuar-viendo?formato=catalogo`)
  await reg('', 'PUT', `/api/v1/collections/${col.id}/progress/${b}`, { estado: 'visto' }); await reg('', 'GET', `/api/v1/collections/${col.id}/items`); await reg('', 'PUT', `/api/v1/collections/${col.id}`, { nombre: 'Renombrada' })
  await reg('', 'GET', '/api/v1/collections/no-existe')
  await reg('', 'PUT', `/api/v1/series/${series[0].id}/progress`, { estado: 'visto', temporada: 1 }); await reg('', 'GET', `/api/v1/series/${series[0].id}/progress-states`); await reg('', 'GET', `/api/v1/libraries/${lib0[0].id}/progress-states`)
  for (const q of ['', '?estado=visto', '?estado=enProgreso', '?orden=-reproducido&tamano=15', '?q=padrino', '?tipo=serie']) await reg('', 'GET', '/api/v1/catalog' + q)
  await reg('', 'GET', '/api/v1/home'); await reg('', 'GET', '/api/v1/people/8229/in-catalog'); await reg('', 'GET', `/api/v1/catalog/items?ids=${a},${b}`); await reg('', 'GET', `/api/v1/items/${a}`); await reg('', 'GET', `/api/v1/series/${series[0].id}/episodes`)
  await reg('', 'DELETE', `/api/v1/collections/${col.id}`); await reg('', 'GET', '/api/v1/collections'); await reg('', 'GET', '/api/v1/admin/bibliotecas')
  return out.join('\n')
}
await pair('proxy con token', secuencia)

// streaming con Range e imágenes a través del proxy
for (const n of ['node', 'qt']) { void n }
for (let i = 0; i < 2; i++) { const w = await fetch(`${URLR}/api/v1/image?src=${encodeURIComponent(pelis[0].posterUrl)}&size=w342`, { headers: { 'X-Api-Key': key } }); await w.arrayBuffer() }
const streamInfo = async (n) => { const ap = api(n); const out = []
  for (const rg of [undefined, 'bytes=0-99', 'bytes=1000-1999', 'bytes=-500']) { const r = await fetch(`${ap.baseUrl}/api/v1/media/${a}/stream`, { headers: { 'X-Api-Key': ap.token, ...(rg ? { Range: rg } : {}) } }); const buf = rg ? Buffer.from(await r.arrayBuffer()) : null; if (!rg) await r.body?.cancel(); out.push(`${rg} ${r.status} ${r.headers.get('content-range')} ${r.headers.get('content-length')} ${r.headers.get('content-type')} ${buf ? buf.length : ''}`) }
  const im = await fetch(`${ap.baseUrl}/api/v1/image?src=${encodeURIComponent(pelis[0].posterUrl)}&size=w342`, { headers: { 'X-Api-Key': ap.token } }); out.push(`img ${im.status} ${im.headers.get('content-type')} ${Buffer.from(await im.arrayBuffer()).length}`)
  const sin = await fetch(`${ap.baseUrl}/api/v1/libraries`); out.push('sin cred ' + sin.status); return out.join('\n') }
await pair('streaming e imagen por el proxy', async (o, n) => streamInfo(n))

// ---- desconectar / reconectar / olvidar
await pair('desconectar', async (o) => { const r = (await J(o, 'POST', '/control/desconectar')).j; return { r, e: (await J(o, 'GET', '/control/estado')).j.conexion } })
const recId = {}; for (const [n, o] of [['node', cn], ['qt', cq]]) recId[n] = (await J(o, 'GET', '/control/estado')).j.recordados[0].id
await pair('reconectar', async (o, n) => { const r = (await J(o, 'POST', '/control/reconectar', { id: recId[n] })).j; con[n] = r.estado; return { ok: r.ok, e: limpiarEstado(r.estado) } })
await pair('personal persiste al reconectar', async (o, n) => { const ap = api(n); return (await J(ap, 'GET', `/api/v1/media/${a}/user-state`, undefined, ap.token, ap.baseUrl)).j })

// ---- con usuario y contraseña (el progreso vive en el servidor)
{ const r = await J(cq, 'POST', '/control/olvidar'); const l = (await J(cq, 'GET', '/control/estado')).j.recordados; same('olvidar (qt): ok y sin recordados', JSON.stringify([r.s, l.length]), '[200,0]') }
await pair('credenciales malas', async (o) => (await J(o, 'POST', '/control/conectar', { url: URLR, credencial: { tipo: 'sesion', usuario: 'ana', password: 'x' }, recordar: true })).j)
const usr = { node: 'ana', qt: 'beto' }
await pair('conectar con usuario', async (o, n) => { const r = (await J(o, 'POST', '/control/conectar', { url: URLR, credencial: { tipo: 'sesion', usuario: usr[n], password: 'secreta1' }, recordar: true })).j; con[n] = r.estado; return { ok: r.ok, e: limpiarEstado(r.estado) } })
for (const o of [cn, cq]) await esperar(() => listo(o))
const secUsuario = async (o, n) => { const ap = api(n); const out = []
  const reg = async (m, path, body) => { const r = await J(ap, m, path, body, ap.token, ap.baseUrl); out.push(`${m} ${path} -> ${r.s} ${JSON.stringify(sort(r.j), (k, v) => (k === 'creadoEn' ? undefined : v)).slice(0, 400)}`) }
  await reg('PUT', `/api/v1/media/${a}/progress`, { posicionSegundos: 500 }); await reg('GET', `/api/v1/media/${a}/user-state`); await reg('PUT', `/api/v1/watchlist/${b}`); await reg('GET', '/api/v1/watchlist?formato=catalogo'); await reg('GET', '/api/v1/home'); await reg('GET', '/api/v1/catalog?estado=enProgreso')
  return out.join('\n') }
await pair('proxy con usuario', secUsuario)

// ---- sesión vencida: el dueño cambia la contraseña (cierra sus sesiones) → la siguiente petición da 401 y el cliente lo detecta
const uid = (await J(rem, 'GET', '/api/v1/admin/usuarios')).j
for (const n of ['node', 'qt']) await J(rem, 'PUT', `/api/v1/admin/usuarios/${uid.find(u => u.nombreUsuario === usr[n]).id}/password`, { password: 'nueva-clave-1' })
for (const [n, o] of [['node', cn], ['qt', cq]]) { const ap = api(n); await fetch(`${ap.baseUrl}/api/v1/libraries`, { headers: { 'X-Api-Key': ap.token } }).then(r => r.text()).catch(() => 0) }
const tipoEstado = async (o) => (await J(o, 'GET', '/control/estado')).j.conexion.tipo
for (const o of [cn, cq]) await esperar(async () => (await tipoEstado(o)) === 'sesion-vencida', 5000)
await pair('sesion vencida', async (o) => limpiarEstado((await J(o, 'GET', '/control/estado')).j.conexion))
await new Promise(r => setTimeout(r, 62000))
await pair('renovar ok', async (o, n) => { const r = (await J(o, 'POST', '/control/renovar', { password: 'nueva-clave-1' })).j; if (r.estado) con[n] = r.estado; if (!r.ok) console.log(n, 'renovar:', JSON.stringify(r)); return { ok: r.ok, e: r.estado ? limpiarEstado(r.estado) : null } })

// ---- servidor caído
await J(rem, 'POST', '/api/v1/admin/servidor/detener')
for (const [n, o] of [['node', cn], ['qt', cq]]) { const c = (await J(o, 'GET', '/control/estado')).j.conexion; if (c.api) con[n] = c }
const caida = {}
for (const [n, o] of [['node', cn], ['qt', cq]]) { const ap = api(n); try { const r = await fetch(`${ap.baseUrl}/api/v1/libraries`, { headers: { 'X-Api-Key': ap.token } }); caida[n] = r.status } catch (e) { caida[n] = 'refused'; console.log('refused', n, JSON.stringify(ap), JSON.stringify((await J(o, 'GET', '/control/estado')).j.conexion)) } }
same('proxy con servidor caido (rechazo o 502)', ['refused','502'].includes(String(caida.node)) ? 'caido' : String(caida.node), ['refused','502'].includes(String(caida.qt)) ? 'caido' : String(caida.qt))
for (const o of [cn, cq]) await esperar(async () => (await tipoEstado(o)) === 'sin-conexion', 8000)
await pair('sin conexion', async (o) => limpiarEstado((await J(o, 'GET', '/control/estado')).j.conexion))
await J(rem, 'POST', '/api/v1/admin/servidor/iniciar')
await pair('reintentar con el servidor de vuelta', async (o, n) => { const r = (await J(o, 'POST', '/control/reintentar')).j; if (r.estado?.api) con[n] = r.estado; return { ok: r.ok, t: r.estado?.tipo } })

await pair('renovar mal', async (o) => ({ ok: (await J(o, 'POST', '/control/renovar', { password: 'incorrecta' })).j.ok }))
console.log(`Resultado: ${ok} iguales, ${fail} con diferencias`)
for (const p of [cn.p, cq.p, rem.p]) p.kill()
await new Promise(r => setTimeout(r, 1500)); try { rmSync(S, { recursive: true, force: true }) } catch {}
process.exit(fail ? 1 : 0)
