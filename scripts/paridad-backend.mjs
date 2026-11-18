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
let ok = 0, fail = 0
const cmp = async (path, ignorar = []) => {
  const a = await get(node, path), b = await get(qt, path)
  const strip = (o) => { if (o && typeof o === 'object' && !Array.isArray(o)) for (const k of ignorar) delete o[k]; return o }
  const same = a.status === b.status && norm(strip(a.json)) === norm(strip(b.json))
  if (same) ok++; else { fail++; console.log('DIFERENCIA', path, a.status, b.status); console.log('  node:', a.text.slice(0, 220)); console.log('  qt  :', b.text.slice(0, 220)) }
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
node.p.kill(); qt.p.kill(); rmSync(S, { recursive: true, force: true }); process.exit(fail ? 1 : 0)
