import { spawn } from 'node:child_process'
// Paridad del backend integrado (C++) con el backend Node, sobre dos copias idénticas de la base. Solo para desarrollo (mientras exista backend/).
// Uso: third_party/node/node.exe scripts/paridad-backend.mjs   (requiere build/alt|win compilado y .dev-data/cinemalove.sqlite)
import { copyFileSync, mkdirSync, rmSync, existsSync, statSync, cpSync } from 'node:fs'
import { createRequire } from 'node:module'
import { fileURLToPath } from 'node:url'
import { dirname, resolve } from 'node:path'
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..').replaceAll(String.fromCharCode(92), '/')
const exe = [root + '/build/win/Release/cinemalove.exe', root + '/build/alt/Release/cinemalove.exe'].filter(existsSync).sort((a, b) => statSync(b).mtimeMs - statSync(a).mtimeMs)[0]
const S = root + '/.dev-data/paridad'
rmSync(S, { recursive: true, force: true }); mkdirSync(S + '/node', { recursive: true }); mkdirSync(S + '/qt', { recursive: true })
copyFileSync(root + '/.dev-data/cinemalove.sqlite', S + '/node/cinemalove.sqlite'); copyFileSync(root + '/.dev-data/cinemalove.sqlite', S + '/qt/cinemalove.sqlite')
// Imágenes ya descargadas y un usuario de prueba (contraseña hasheada con bcryptjs) en ambas copias
const req = createRequire(root + '/backend/package.json')
const bcrypt = req('bcryptjs'), Sqlite = req('better-sqlite3')
const hashPrueba = bcrypt.hashSync('contraseña-ñ-123', 10)
for (const d of ['node', 'qt']) {
  if (existsSync(root + '/.dev-data/images')) cpSync(root + '/.dev-data/images', S + '/' + d + '/images', { recursive: true })
  const db = new Sqlite(S + '/' + d + '/cinemalove.sqlite')
  db.prepare("INSERT INTO usuarios (id, nombre_usuario, password_hash, rol, creado_en) VALUES ('u-prueba', 'ana', ?, 'cliente', 1700000000)").run(hashPrueba)
  db.close()
}
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
  const strip = (o) => o && typeof o === 'object' ? JSON.parse(JSON.stringify(o, (k, v) => (ignorar.includes(k) ? undefined : v))) : o
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
// inicio de sesión, usuarios y listas por usuario
{
  const login = async (o, body) => { const r = await fetch(o.baseUrl + '/api/v1/login', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }); return { status: r.status, json: await r.json().catch(() => null) } }
  const casos = [{ nombreUsuario: 'ana', password: 'contraseña-ñ-123' }, { nombreUsuario: 'ana', password: 'mala' }, { nombreUsuario: 'nadie', password: 'x' }, { nombreUsuario: 'ana' }, {}]
  for (const c of casos) {
    const a = await login(node, c), b = await login(qt, c)
    const lim = (x) => JSON.stringify(sort({ status: x.status, ...(x.json?.token ? { ...x.json, token: 'T' } : x.json) }))
    if (lim(a) === lim(b)) ok++; else { fail++; console.log('DIFERENCIA login', JSON.stringify(c), lim(a), lim(b)) }
    if (a.json?.token && b.json?.token) {
      for (const ruta of ['/api/v1/me', '/api/v1/collections', '/api/v1/watchlist', '/api/v1/home']) {
        const x = await fetch(node.baseUrl + ruta, { headers: { 'X-Api-Key': a.json.token } }), y = await fetch(qt.baseUrl + ruta, { headers: { 'X-Api-Key': b.json.token } })
        const tx = await x.text(), ty = await y.text()
        if (x.status === y.status && norm(JSON.parse(tx)) === norm(JSON.parse(ty))) ok++; else { fail++; console.log('DIFERENCIA con sesión', ruta, x.status, y.status, tx.slice(0, 150), ty.slice(0, 150)) }
      }
      const lo1 = await fetch(node.baseUrl + '/api/v1/logout', { method: 'POST', headers: { 'X-Api-Key': a.json.token } }), lo2 = await fetch(qt.baseUrl + '/api/v1/logout', { method: 'POST', headers: { 'X-Api-Key': b.json.token } })
      const d1 = await fetch(node.baseUrl + '/api/v1/me', { headers: { 'X-Api-Key': a.json.token } }), d2 = await fetch(qt.baseUrl + '/api/v1/me', { headers: { 'X-Api-Key': b.json.token } })
      if (lo1.status === lo2.status && d1.status === d2.status && d1.status === 401) ok++; else { fail++; console.log('DIFERENCIA logout', lo1.status, lo2.status, d1.status, d2.status) }
    }
  }
}
// imágenes y personas (con red solo si falta en disco)
{
  const urls = [...new Set(pelis.slice(0, 4).map(m => m.posterUrl).filter(Boolean))]
  for (const u of urls) for (const sz of ['', '&size=w342', '&size=original', '&size=raro']) {
    const ruta = '/api/v1/image?src=' + encodeURIComponent(u) + sz
    const x = await fetch(node.baseUrl + ruta, { headers: { 'X-Api-Key': node.token } }), y = await fetch(qt.baseUrl + ruta, { headers: { 'X-Api-Key': qt.token } })
    const bx = Buffer.from(await x.arrayBuffer()), by = Buffer.from(await y.arrayBuffer())
    const hx = (k) => x.headers.get(k), hy = (k) => y.headers.get(k)
    if (x.status === y.status && bx.equals(by) && hx('content-type') === hy('content-type') && hx('cache-control') === hy('cache-control')) ok++; else { fail++; console.log('DIFERENCIA imagen', ruta.slice(0, 120), x.status, y.status, bx.length, by.length, hx('content-type'), hy('content-type')) }
  }
  for (const r of ['/api/v1/image', '/api/v1/image?src=http://x.com/a.jpg', '/api/v1/image?src=https://image.tmdb.org/t/p/w500/../a.jpg']) await cmp(r)
  const noDisp = '/api/v1/image?src=' + encodeURIComponent('https://image.tmdb.org/t/p/w500/noexiste-xyz.jpg')
  await cmp(noDisp)
  for (const id of ['8229', '1', 'x']) await cmp('/api/v1/people/' + id)
}
// Administración: bibliotecas, escáner, trabajos, huérfanos, re-vinculación, homologación y usuarios.
// Se crean dos árboles idénticos de archivos de prueba (uno por backend) para poder renombrar en disco sin que se estorben.
{
  const { mkdirSync: mk, writeFileSync: wf, renameSync: rn, rmSync: rm2 } = await import('node:fs')
  const crear = (raiz) => {
    const f = (rel, n) => { const p = raiz + '/' + rel; mk(p.slice(0, p.lastIndexOf('/')), { recursive: true }); wf(p, Buffer.alloc(200000 + n, n % 251)) }
    f('peliculas/Matrix (1999)/Matrix.1999.1080p.mkv', 1); f('peliculas/Matrix (1999)/Matrix.1999.es.srt', 2); f('peliculas/Matrix (1999)/extras/Making.of.mkv', 3)
    f('peliculas/1917/1917.2019.720p.x264.mp4', 4); f('peliculas/CD1/Pelicula.Rara.2001.avi', 5); f('peliculas/Sin Video/leeme.txt', 6)
    f('series/Dark/Season 1/Dark.S01E01.mkv', 7); f('series/Dark/Season 1/Dark.S01E02.mkv', 8); f('series/Dark/Season 2/dark 2x01.mkv', 9)
    f('series/Show/Episodio 02.mkv', 10); f('series/Show/Episodio 01.mkv', 11); f('series/Show/Episodio 10.mkv', 12)
    f('series/Otra/Temporada 03/Otra S03E04.mp4', 13)
  }
  const A = '/api/v1/admin'
  const adm = async (o, m, path, body) => { const r = await fetch(o.baseUrl + A + path, { method: m, headers: body !== undefined ? { 'X-Api-Key': o.token, 'Content-Type': 'application/json' } : { 'X-Api-Key': o.token }, body: body !== undefined ? JSON.stringify(body) : undefined }); const j = await r.json().catch(() => null); return { status: r.status, json: j } }
  const esperar = async (o, id) => { for (let i = 0; i < 200; i++) { const j = (await adm(o, 'GET', '/jobs/' + id)).json; if (j && j.estado !== 'corriendo') return j; await new Promise(r => setTimeout(r, 100)) } return null }
  const secAdmin = async (o, raiz) => {
    const out = []
    const ph = (x) => JSON.stringify(sort(x), (k, v) => (['creadoEn', 'terminadoEn', 'id', 'jobId', 'libraryId'].includes(k) ? undefined : v)).split(String.fromCharCode(92) + String.fromCharCode(92)).join('/').split(raiz).join('RAIZ')
    const reg = (t, r) => out.push(`${t} -> ${r.status} ${ph(r.json)}`)
    const libsAntes = (await adm(o, 'GET', '/bibliotecas')).json.length
    reg('crear sin nombre', await adm(o, 'POST', '/bibliotecas', { tipo: 'pelicula', ruta: raiz + '/peliculas' }))
    reg('crear tipo malo', await adm(o, 'POST', '/bibliotecas', { nombre: 'X', tipo: 'otro', ruta: raiz + '/peliculas' }))
    reg('crear ruta inexistente', await adm(o, 'POST', '/bibliotecas', { nombre: 'X', tipo: 'pelicula', ruta: raiz + '/nada' }))
    const c1 = await adm(o, 'POST', '/bibliotecas', { nombre: 'Prueba Pelis', tipo: 'pelicula', ruta: raiz + '/peliculas', escanear: false }); reg('crear pelis', c1)
    reg('crear nombre repetido', await adm(o, 'POST', '/bibliotecas', { nombre: 'prueba pelis', tipo: 'pelicula', ruta: raiz + '/series' }))
    reg('crear superpuesta', await adm(o, 'POST', '/bibliotecas', { nombre: 'Dentro', tipo: 'pelicula', ruta: raiz + '/peliculas/Matrix (1999)' }))
    const c2 = await adm(o, 'POST', '/bibliotecas', { nombre: 'Prueba Series', tipo: 'serie', ruta: raiz + '/series', escanear: false }); reg('crear series', c2)
    const lp = c1.json.id, ls = c2.json.id
    const lista = (await adm(o, 'GET', '/bibliotecas')).json.filter(l => l.id === lp || l.id === ls); out.push('lista: ' + ph(lista))
    const correr = async (id, ruta, etiqueta) => { const j = await adm(o, 'POST', ruta); if (j.status !== 200) { reg(etiqueta, j); return }; const fin = await esperar(o, j.json.id); const det = (await adm(o, 'GET', '/jobs/' + j.json.id)).json; out.push(`${etiqueta}: ${ph({ estado: fin?.estado, resultado: fin?.resultado, error: fin?.error, lineas: det?.lineas })}`) }
    const foto = async (etiqueta) => {
      const filas = []
      for (const [lid, tipo] of [[lp, 'pelicula'], [ls, 'serie']]) {
        const items = tipo === 'pelicula' ? (await get(o, `/api/v1/libraries/${lid}/items`)).json : []
        for (const m of items) { const a = (await adm(o, 'GET', `/items/${m.id}/archivo`)).json; filas.push(`P|${a.rutaRelativa}|${a.existe}|${a.bytes}`) }
        const ss = tipo === 'serie' ? (await get(o, `/api/v1/libraries/${lid}/series`)).json : []
        for (const sx of ss) {
          for (const e of (await get(o, `/api/v1/series/${sx.id}/episodes`)).json) { const a = (await adm(o, 'GET', `/items/${e.id}/archivo`)).json; filas.push(`E|${a.rutaRelativa}|T${e.numeroTemporada}E${e.numeroEpisodio}`) }
        }
        for (const h of (await adm(o, 'GET', `/bibliotecas/${lid}/huerfanos`)).json) filas.push(`H|${h.tipo}|${h.rutaRelativa}|${h.serie}`)
      }
      out.push(`${etiqueta}: ` + ph(filas.sort()))
    }
    await correr(lp, `/bibliotecas/${lp}/escanear`, 'escaneo pelis'); await correr(ls, `/bibliotecas/${ls}/escanear`, 'escaneo series'); await foto('estado 1')
    await correr(lp, `/bibliotecas/${lp}/escanear`, 'reescaneo pelis'); await foto('estado 2')
    // mover un archivo (se reconcilia por hash), borrar otro (queda huérfano) y añadir uno nuevo
    rn(raiz + '/peliculas/1917/1917.2019.720p.x264.mp4', raiz + '/peliculas/1917/1917.renombrada.mp4')
    rm2(raiz + '/peliculas/CD1/Pelicula.Rara.2001.avi')
    mk(raiz + '/peliculas/Nueva (2020)', { recursive: true }); wf(raiz + '/peliculas/Nueva (2020)/Nueva.2020.mkv', Buffer.alloc(300001, 7))
    rn(raiz + '/series/Dark/Season 1/Dark.S01E02.mkv', raiz + '/series/Dark/Season 1/Dark.S01E02.FINAL.mkv')
    await correr(lp, '/escanear-todo', 'escanear todo'); await foto('estado 3')
    const h = (await adm(o, 'GET', `/bibliotecas/${lp}/huerfanos`)).json; reg('huerfanos', { status: 200, json: h })
    reg('borrar huerfanos cantidad mala', await adm(o, 'DELETE', `/bibliotecas/${lp}/huerfanos?confirm=99`))
    const nuevo = (await get(o, `/api/v1/libraries/${lp}/items`)).json.find(m => m.titulo == null && m.id)
    // re-vincular el huérfano con otro archivo existente
    if (h.length) { reg('revincular fuera', await adm(o, 'POST', `/items/${h[0].id}/revincular`, { rutaRelativa: '../x.mkv' })); reg('revincular inexistente', await adm(o, 'POST', `/items/${h[0].id}/revincular`, { rutaRelativa: 'nada/x.mkv' })); reg('revincular ocupado', await adm(o, 'POST', `/items/${h[0].id}/revincular`, { rutaRelativa: 'Matrix (1999)/Matrix.1999.1080p.mkv' }))
      reg('revincular ok', await adm(o, 'POST', `/items/${h[0].id}/revincular`, { rutaRelativa: 'Nueva (2020)/Nueva.2020.mkv' })) }
    await foto('estado 4')
    // homologar: vista previa y aplicación
    const matrix = (await get(o, `/api/v1/libraries/${lp}/items`)).json
    for (const m of matrix) { const a = (await adm(o, 'GET', `/items/${m.id}/archivo`)).json
      if (a.rutaRelativa.startsWith('Matrix')) { reg('homologar previa', await adm(o, 'POST', `/items/${m.id}/homologar`, { titulo: 'The Matrix: Reloaded?', anio: 2003 })); reg('homologar anio malo', await adm(o, 'POST', `/items/${m.id}/homologar`, { titulo: 'X', anio: 3000 })); reg('homologar aplicar', await adm(o, 'POST', `/items/${m.id}/homologar`, { titulo: 'The Matrix: Reloaded?', anio: 2003, aplicar: true })); reg('homologar de nuevo', await adm(o, 'POST', `/items/${m.id}/homologar`, { titulo: 'The Matrix: Reloaded?', anio: 2003 })) } }
    await foto('estado 5')
    // metadata manual
    const un = (await get(o, `/api/v1/libraries/${lp}/items`)).json[0]
    reg('editar metadata', await adm(o, 'PUT', `/items/${un.id}/metadata`, { titulo: 'Mi título', anio: 1999, sinopsis: '  ', generos: ['Acción', 'Drama'], reparto: [{ id: 5, nombre: 'Ana', rol: 'X' }, { nombre: 'Sin id' }], duracionSegundos: 3600 }))
    reg('editar metadata ruta mala', await adm(o, 'PUT', `/otra/${un.id}/metadata`, { titulo: 'x' })); reg('editar metadata no existe', await adm(o, 'PUT', `/items/nada/metadata`, { titulo: 'x' }))
    out.push('ficha: ' + ph((await get(o, `/api/v1/items/${un.id}`)).json))
    // ajustes
    reg('tmdb get', await adm(o, 'GET', '/ajustes/tmdb')); reg('subs get', await adm(o, 'GET', '/ajustes/subtitulos'))
    reg('subs put', await adm(o, 'PUT', '/ajustes/subtitulos', { tamano: 40, posicion: 60 })); reg('subs put malo', await adm(o, 'PUT', '/ajustes/subtitulos', { tamano: 'x' }))
    reg('limpiar raro', await adm(o, 'POST', '/datos/limpiar/otra'))
    // usuarios y token
    reg('usuario corto', await adm(o, 'POST', '/usuarios', { nombreUsuario: 'ab', password: '123456' })); reg('usuario pass corta', await adm(o, 'POST', '/usuarios', { nombreUsuario: 'carlos', password: '123' }))
    const u = await adm(o, 'POST', '/usuarios', { nombreUsuario: 'carlos', password: 'secreta1' }); reg('usuario ok', { status: u.status, json: { ...u.json, id: undefined } })
    reg('usuario repetido', await adm(o, 'POST', '/usuarios', { nombreUsuario: 'CARLOS', password: 'secreta1' }))
    out.push('usuarios: ' + ph((await adm(o, 'GET', '/usuarios')).json.map(x => ({ n: x.nombreUsuario, rol: x.rol, s: x.sesiones }))))
    const lg = await fetch(o.baseUrl + '/api/v1/login', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ nombreUsuario: 'carlos', password: 'secreta1' }) }); out.push('login nuevo usuario: ' + lg.status)
    reg('pass corta', await adm(o, 'PUT', `/usuarios/${u.json.id}/password`, { password: '1' })); reg('pass ok', await adm(o, 'PUT', `/usuarios/${u.json.id}/password`, { password: 'otra-clave' }))
    const lg2 = await fetch(o.baseUrl + '/api/v1/login', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ nombreUsuario: 'carlos', password: 'otra-clave' }) }); out.push('login tras cambio: ' + lg2.status)
    reg('borrar sin confirmar', await adm(o, 'DELETE', `/usuarios/${u.json.id}?confirm=otro`)); reg('borrar ok', await adm(o, 'DELETE', `/usuarios/${u.json.id}?confirm=carlos`))
    const k = (await adm(o, 'POST', '/api-key/regenerar')).json.valor; out.push('api-key regenerada: ' + (k.length) + ' ' + /^[0-9a-f]{64}$/.test(k))
    out.push('api-key get igual: ' + ((await adm(o, 'GET', '/api-key')).json.valor === k))
    // 403 para quien no es admin
    const nr = await fetch(o.baseUrl + A + '/bibliotecas', { headers: { 'X-Api-Key': k } }); out.push('apikey no admin: ' + nr.status)
    // borrar biblioteca
    reg('borrar con nombre mal', await adm(o, 'DELETE', `/bibliotecas/${lp}?confirm=x`)); reg('borrar pelis', await adm(o, 'DELETE', `/bibliotecas/${lp}?confirm=Prueba%20Pelis`)); reg('borrar series', await adm(o, 'DELETE', `/bibliotecas/${ls}?confirm=Prueba%20Series`))
    out.push('bibliotecas al final: ' + ((await adm(o, 'GET', '/bibliotecas')).json.length === libsAntes))
    // trabajos inexistentes
    reg('job no existe', await adm(o, 'GET', '/jobs/nada')); reg('cancelar no existe', await adm(o, 'POST', '/jobs/nada/cancelar'))
    return out
  }
  const rn1 = S + '/media-node', rq1 = S + '/media-qt'
  crear(rn1); crear(rq1)
  const a = await secAdmin(node, rn1), b = await secAdmin(qt, rq1)
  for (let i = 0; i < Math.max(a.length, b.length); i++) {
    if (a[i] === b[i]) ok++; else { fail++; console.log('DIFERENCIA admin #' + i); const x = a[i] ?? '', y = b[i] ?? ''; let k = 0; while (k < x.length && x[k] === y[k]) k++; console.log('  primera diferencia en', k); console.log('  node:', x.slice(Math.max(0, k - 120), k + 200)); console.log('  qt  :', y.slice(Math.max(0, k - 120), k + 200)) }
  }
}
// Importación de metadata desde TMDb (requiere red y la clave de la base de pruebas): mismo resultado en ambos
{
  const A = '/api/v1/admin'
  const adm = async (o, m, path, body) => { const r = await fetch(o.baseUrl + A + path, { method: m, headers: body !== undefined ? { 'X-Api-Key': o.token, 'Content-Type': 'application/json' } : { 'X-Api-Key': o.token }, body: body !== undefined ? JSON.stringify(body) : undefined }); return { status: r.status, json: await r.json().catch(() => null) } }
  const correr = async (o, lid) => {
    const j = await adm(o, 'POST', `/bibliotecas/${lid}/metadata`, { modo: 'faltantes' }); if (j.status !== 200) return `inicio ${j.status} ${JSON.stringify(j.json)}`
    for (let i = 0; i < 600; i++) { const d = (await adm(o, 'GET', '/jobs/' + j.json.id)).json; if (d.estado !== 'corriendo') return JSON.stringify(sort({ estado: d.estado, error: d.error, resultado: d.resultado, lineas: d.lineas })); await new Promise(r => setTimeout(r, 200)) }
    return 'timeout'
  }
  const lib = libs.find(l => l.tipo === 'pelicula')
  const uu = (t) => t.replace(/[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}/g, 'U')
  const a = uu(await correr(node, lib.id)), b = uu(await correr(qt, lib.id))
  if (a === b) ok++; else { fail++; console.log('DIFERENCIA metadata job'); let k = 0; while (k < a.length && a[k] === b[k]) k++; console.log('  en', k); console.log('  node:', a.slice(Math.max(0, k - 150), k + 250)); console.log('  qt  :', b.slice(Math.max(0, k - 150), k + 250)) }
  await cmp('/api/v1/libraries/' + lib.id + '/items', ['creadoEn', 'id'])
  const sinMeta = pelis.filter(m => m.titulo == null).map(m => m.id)
  for (const id of sinMeta) await cmp('/api/v1/items/' + id, ['creadoEn', 'id'])
  console.log('metadata: probados', sinMeta.length, 'ítems sin metadata previa;', a.slice(0, 160))
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
