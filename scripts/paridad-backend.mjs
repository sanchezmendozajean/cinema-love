import { spawn } from 'node:child_process'
// Paridad del backend integrado (C++) con el backend Node, sobre dos copias idénticas de la base. Solo para desarrollo (mientras exista backend/).
// Uso: third_party/node/node.exe scripts/paridad-backend.mjs   (requiere build/alt|win compilado y .dev-data/cinemalove.sqlite)
import { copyFileSync, mkdirSync, rmSync, existsSync, statSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, resolve } from 'node:path'
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..').replaceAll(String.fromCharCode(92), '/')
const exe = [root + '/build/win/Release/cinemalove.exe', root + '/build/alt/Release/cinemalove.exe'].filter(existsSync).sort((a, b) => statSync(b).mtimeMs - statSync(a).mtimeMs)[0]
const S = root + '/.dev-data/paridad'
rmSync(S, { recursive: true, force: true }); mkdirSync(S + '/node', { recursive: true }); mkdirSync(S + '/qt', { recursive: true })
copyFileSync(root + '/.dev-data/cinemalove.sqlite', S + '/node/cinemalove.sqlite'); copyFileSync(root + '/.dev-data/cinemalove.sqlite', S + '/qt/cinemalove.sqlite')
const lanzar = (cmd, args, env) => new Promise((res, rej) => {
  const p = spawn(cmd, args, { env: { ...process.env, ...env }, stdio: ['pipe', 'pipe', 'inherit'] })
  let b = ''; p.stdout.on('data', d => { b += d; const m = /CL_(READY|ERROR) (.*)/.exec(b); if (m) m[1] === 'READY' ? res({ p, ...JSON.parse(m[2]) }) : rej(new Error(m[2])) })
})
const node = await lanzar(root + '/third_party/node/node.exe', [root + '/backend/dist/main.cjs'], { CL_APP_ROOT: root, CL_SIN_FFMPEG: '1', CL_DATA_DIR: S + '/node' })
const qt = await lanzar(exe, ['--serve', S + '/qt'], { PATH: 'D:/Qt/6.8.3/msvc2022_64/bin;' + process.env.PATH })
const get = async (o, path, headers = {}) => { const r = await fetch(o.baseUrl + path, { headers: { 'X-Api-Key': o.token, ...headers } }); const t = await r.text(); let j = null; try { j = JSON.parse(t) } catch {} return { status: r.status, json: j, len: t.length, h: r.headers, text: t } }
const norm = (x) => JSON.stringify(sort(x))
const sort = (x) => Array.isArray(x) ? x.map(sort) : x && typeof x === 'object' ? Object.fromEntries(Object.keys(x).sort().map(k => [k, sort(x[k])])) : x
const primera = (a, b, ruta = '') => {
  if (JSON.stringify(a) === JSON.stringify(b)) return null
  if (a && b && typeof a === 'object' && typeof b === 'object') {
    for (const k of new Set([...Object.keys(a), ...Object.keys(b)])) { const d = primera(a[k], b[k], ruta + '/' + k); if (d) return d }
  }
  return `${ruta}: node=${JSON.stringify(a)?.slice(0, 120)} qt=${JSON.stringify(b)?.slice(0, 120)}`
}
let ok = 0, fail = 0
const cmp = async (path, ignorar = []) => {
  const a = await get(node, path), b = await get(qt, path)
  const strip = (o) => { if (o && typeof o === 'object' && !Array.isArray(o)) for (const k of ignorar) delete o[k]; return o }
  const same = a.status === b.status && norm(strip(a.json)) === norm(strip(b.json))
  if (same) ok++; else { fail++; console.log('DIFERENCIA', path, a.status, b.status, primera(sort(strip(a.json)), sort(strip(b.json))) ?? '') }
}
await cmp('/api/v1/health', ['nombre', 'plataforma'])
await cmp('/api/v1/me')
await cmp('/api/v1/libraries')
const libs = (await get(node, '/api/v1/libraries')).json
const pelis = [], series = []
for (const l of libs) {
  if (l.tipo === 'pelicula') { await cmp(`/api/v1/libraries/${l.id}/items`); pelis.push(...(await get(node, `/api/v1/libraries/${l.id}/items`)).json) }
  else { await cmp(`/api/v1/libraries/${l.id}/series`); series.push(...(await get(node, `/api/v1/libraries/${l.id}/series`)).json) }
  await cmp(`/api/v1/libraries/${l.id}/${l.tipo === 'pelicula' ? 'series' : 'items'}`) // error 400
}
for (const m of pelis) { await cmp(`/api/v1/items/${m.id}`); await cmp(`/api/v1/items/${m.id}/extras`) }
for (const s of series) { await cmp(`/api/v1/series/${s.id}`); await cmp(`/api/v1/series/${s.id}/seasons`); await cmp(`/api/v1/series/${s.id}/episodes`); await cmp(`/api/v1/series/${s.id}/episodes?temporada=1`); await cmp(`/api/v1/series/${s.id}/episodes?temporada=x`) }
await cmp('/api/v1/items/no-existe'); await cmp('/api/v1/series/no-existe'); await cmp('/api/v1/ruta-rara')
// catálogo unificado e Inicio
for (const q of ['', '?tamano=20', '?tamano=20&pagina=3', '?orden=-anio&tamano=30', '?orden=anio', '?orden=-agregado', '?orden=reproducido', '?orden=-titulo&tamano=15',
  '?q=padrino', '?q=a', '?q=dalmatas', '?q=el%20rey', '?tipo=pelicula&tamano=10', '?tipo=serie', '?generos=Drama', '?generos=Drama,Comedia&tamano=25', '?anioDesde=1990&anioHasta=1999',
  '?estado=visto', '?estado=enProgreso', '?estado=noVisto&tamano=10', '?q=acci%C3%B3n&generos=Acci%C3%B3n', '?tamano=500', '?tamano=0', '?pagina=0', '?tipo=x', '?estado=x', '?orden=x', '?pagina=9999'])
  await cmp('/api/v1/catalog' + q)
await cmp('/api/v1/home')
{
  const ids = pelis.slice(0, 30).map(m => m.id).join(',')
  await cmp('/api/v1/catalog/items?ids=' + ids); await cmp('/api/v1/catalog/continuar?ids=' + ids)
  await cmp('/api/v1/catalog/items?ids=' + series.slice(0, 5).map(m => m.id).join(',') + ',' + ids.split(',').slice(0, 3).join(','))
  await cmp('/api/v1/catalog/items'); await cmp('/api/v1/catalog/continuar')
}
// Datos personales: se aplica la MISMA secuencia de operaciones a ambos y se comparan las respuestas y el estado resultante
const secuencia = async (o) => {
  const out = []
  const ids = new Map()
  const limpiar = (x) => { const t = JSON.stringify(sort(x), (k, v) => (k === 'creadoEn' ? undefined : v)); let r = t ?? 'null'; for (const [real, ph] of ids) r = r.split(real).join(ph); return r }
  const call = async (m, path, body) => {
    const r = await fetch(o.baseUrl + path, { method: m, headers: body !== undefined ? { 'X-Api-Key': o.token, 'Content-Type': 'application/json' } : { 'X-Api-Key': o.token }, body: body !== undefined ? JSON.stringify(body) : undefined })
    const j = await r.json().catch(() => null); if (m === 'POST' && path === '/api/v1/collections' && j?.id) ids.set(j.id, 'COL1'); out.push(`${m} ${path.replace(/[0-9a-f]{8}-[0-9a-f-]{27}/g, (x) => ids.get(x) ?? x)} -> ${r.status} ${limpiar(j)}`); return j
  }
  const [a, b, c] = pelis.slice(5, 8).map(m => m.id)
  await call('PUT', `/api/v1/media/${a}/progress`, { posicionSegundos: 1234, pistaAudioId: '2', pistaSubtituloId: '3' })
  await call('GET', `/api/v1/media/${a}/progress`)
  await call('PUT', `/api/v1/media/${a}/progress`, { posicionSegundos: 2000 })
  await call('PUT', `/api/v1/media/${b}/progress`, { estado: 'visto' })
  await call('PUT', `/api/v1/media/${b}/progress`, { estado: 'noVisto' })
  await call('PUT', `/api/v1/media/${c}/progress`, { estado: 'raro' })
  await call('PUT', `/api/v1/media/no-existe/progress`, { posicionSegundos: 1 })
  await call('GET', `/api/v1/media/${a}/user-state`)
  await call('PUT', `/api/v1/watchlist/${a}`); await call('PUT', `/api/v1/watchlist/${b}`); await call('PUT', `/api/v1/watchlist/${a}`)
  await call('GET', '/api/v1/watchlist'); await call('GET', '/api/v1/watchlist?formato=catalogo')
  await call('DELETE', `/api/v1/watchlist/${b}`); await call('GET', '/api/v1/watchlist?formato=catalogo')
  const col = await call('POST', '/api/v1/collections', { nombre: '  Prueba  ', quitarAlVerse: true })
  await call('POST', '/api/v1/collections', { nombre: '' }); await call('POST', '/api/v1/collections', {})
  await call('POST', `/api/v1/collections/${col.id}/items`, { mediaItemId: a }); await call('POST', `/api/v1/collections/${col.id}/items`, { mediaItemId: b }); await call('POST', `/api/v1/collections/${col.id}/items`, { mediaItemId: c })
  await call('POST', `/api/v1/collections/${col.id}/items`, { mediaItemId: 'x' }); await call('POST', `/api/v1/collections/${col.id}/items`, {})
  await call('GET', '/api/v1/collections'); await call('GET', `/api/v1/collections/${col.id}`); await call('GET', `/api/v1/collections/${col.id}/items`); await call('GET', `/api/v1/collections/${col.id}/items?formato=catalogo`)
  await call('GET', `/api/v1/collections/item/${a}`); await call('GET', `/api/v1/media/${a}/user-state`)
  await call('PUT', `/api/v1/collections/${col.id}`, { nombre: 'Renombrada', quitarAlVerse: false }); await call('PUT', `/api/v1/collections/${col.id}`, { nombre: '' })
  await call('PUT', `/api/v1/collections/${col.id}/progress/${a}`, { posicionSegundos: 77 }); await call('GET', `/api/v1/collections/${col.id}/progress/${a}`)
  await call('GET', `/api/v1/collections/${col.id}/continuar-viendo`); await call('GET', `/api/v1/collections/${col.id}/continuar-viendo?formato=catalogo`)
  await call('GET', `/api/v1/collections/${col.id}/items?formato=catalogo`)
  await call('PUT', `/api/v1/collections/${col.id}/progress/${b}`, { estado: 'visto' }); await call('GET', `/api/v1/collections/${col.id}/items`)
  await call('PUT', `/api/v1/collections/${col.id}/progress/${a}`, { estado: 'noVisto' }); await call('GET', `/api/v1/collections/${col.id}/progress/${a}`)
  await call('DELETE', `/api/v1/collections/${col.id}/items/${a}`); await call('GET', `/api/v1/collections/${col.id}/items`)
  await call('GET', '/api/v1/collections/no-existe'); await call('DELETE', `/api/v1/collections/${col.id}`); await call('GET', '/api/v1/collections')
  // series: marcar temporada / serie
  const sid = series[0].id
  await call('PUT', `/api/v1/series/${sid}/progress`, { estado: 'visto', temporada: 1 }); await call('GET', `/api/v1/series/${sid}/progress-states`)
  await call('PUT', `/api/v1/series/${sid}/progress`, { estado: 'noVisto' }); await call('GET', `/api/v1/series/${sid}/progress-states`)
  await call('PUT', `/api/v1/series/${sid}/progress`, { estado: 'x' }); await call('PUT', `/api/v1/series/no-existe/progress`, { estado: 'visto' })
  await call('GET', `/api/v1/libraries/${libs[0].id}/progress-states`); await call('GET', '/api/v1/libraries/no-existe/progress-states')
  // estado agregado tras los cambios
  await call('GET', '/api/v1/home'); await call('GET', '/api/v1/catalog?estado=visto'); await call('GET', '/api/v1/catalog?orden=-reproducido&tamano=15')
  await call('GET', '/api/v1/people/8229/in-catalog'); await call('GET', '/api/v1/people/x/in-catalog')
  return out
}
{
  const a = await secuencia(node), b = await secuencia(qt)
  for (let i = 0; i < Math.max(a.length, b.length); i++) {
    if (a[i] === b[i]) ok++; else { fail++; console.log('DIFERENCIA secuencia #' + i); console.log('  node:', (a[i] ?? '').slice(0, 300)); console.log('  qt  :', (b[i] ?? '').slice(0, 300)) }
  }
}
// streaming con Range
const id = pelis[0].id
for (const rg of [undefined, 'bytes=0-99', 'bytes=1000-', 'bytes=-500', 'bytes=999999999999-', 'basura']) {
  const h = rg ? { Range: rg } : {}
  const a = await fetch(node.baseUrl + `/api/v1/media/${id}/stream`, { headers: { 'X-Api-Key': node.token, ...h } }), b = await fetch(qt.baseUrl + `/api/v1/media/${id}/stream`, { headers: { 'X-Api-Key': qt.token, ...h } })
  const ba = rg === undefined ? null : Buffer.from(await a.arrayBuffer()), bb = rg === undefined ? null : Buffer.from(await b.arrayBuffer())
  if (rg === undefined) { await a.body?.cancel(); await b.body?.cancel() }
  const same = a.status === b.status && a.headers.get('content-range') === b.headers.get('content-range') && a.headers.get('content-length') === b.headers.get('content-length') && a.headers.get('content-type') === b.headers.get('content-type') && (ba === null || ba.equals(bb))
  if (same) ok++; else { fail++; console.log('DIFERENCIA stream', rg, a.status, b.status, a.headers.get('content-range'), b.headers.get('content-range'), a.headers.get('content-length'), b.headers.get('content-length')) }
}
// auth
const sin = await fetch(qt.baseUrl + '/api/v1/libraries'); const nodeSin = await fetch(node.baseUrl + '/api/v1/libraries')
if (sin.status === nodeSin.status) ok++; else { fail++; console.log('DIFERENCIA sin credencial', nodeSin.status, sin.status) }
console.log(`Resultado: ${ok} iguales, ${fail} con diferencias`)
node.p.kill(); qt.p.kill(); await new Promise(r => setTimeout(r, 1500)); try { rmSync(S, { recursive: true, force: true }) } catch {} process.exit(fail ? 1 : 0)
