// Paridad del servidor público integrado (C++) con lo que hacía el backend Node: QR, «Reproduciendo ahora», HTTPS y mDNS.
// Uso: third_party/node/node.exe scripts/paridad-servidor.mjs   (requiere build/alt|win compilado y .dev-data/cinemalove.sqlite)
import { spawn } from 'node:child_process'
import { copyFileSync, mkdirSync, rmSync, existsSync, statSync } from 'node:fs'
import { createRequire } from 'node:module'
import { fileURLToPath } from 'node:url'
import { dirname, resolve } from 'node:path'
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..').split(String.fromCharCode(92)).join('/')
const exe = [root + '/build/win/Release/cinemalove.exe', root + '/build/alt/Release/cinemalove.exe'].filter(existsSync).sort((a, b) => statSync(b).mtimeMs - statSync(a).mtimeMs)[0]
const S = root + '/.dev-data/paridad-srv'
rmSync(S, { recursive: true, force: true }); mkdirSync(S, { recursive: true })
copyFileSync(root + '/.dev-data/cinemalove.sqlite', S + '/cinemalove.sqlite')
const req = createRequire(root + '/backend/package.json')
const QRCode = req('qrcode')
const PATH = 'D:/Qt/6.8.3/msvc2022_64/bin;' + process.env.PATH
let ok = 0, fail = 0
const check = (et, cond, extra = '') => { if (cond) ok++; else { fail++; console.log('FALLO', et, extra) } }
const lanzar = () => new Promise((res, rej) => {
  const p = spawn(exe, ['--serve', S], { env: { ...process.env, PATH }, stdio: ['pipe', 'pipe', 'inherit'] })
  let b = ''; p.stdout.on('data', d => { b += d; const m = /CL_(READY|ERROR) (.*)/.exec(b); if (m) m[1] === 'READY' ? res({ p, ...JSON.parse(m[2]) }) : rej(new Error(m[2])) })
})
const rem = await lanzar()
const J = async (m, path, body) => { const r = await fetch(rem.baseUrl + path, { method: m, headers: { 'X-Api-Key': rem.token, ...(body !== undefined ? { 'Content-Type': 'application/json' } : {}) }, body: body !== undefined ? JSON.stringify(body) : undefined }); return { s: r.status, j: await r.json().catch(() => null) } }
const parar = () => { try { rem.p.kill() } catch {} }
process.on('uncaughtException', (e) => { console.log('EXCEPCION', e.stack); parar(); process.exit(2) })

// ---- QR: las matrices deben ser idénticas a las de la biblioteca de referencia
const matrizDeSvg = (svg) => {
  const n = Number(/viewBox="0 0 (\d+)/.exec(svg)[1]) - 2
  const m = Array.from({ length: n }, () => Array(n).fill(0))
  const d = /fill="#000000" d="([^"]*)"/.exec(svg)[1]
  for (const t of d.matchAll(/M(\d+) (\d+)h(\d+)v1h-\d+z/g)) for (let i = 0; i < Number(t[3]); i++) m[Number(t[2]) - 1][Number(t[1]) - 1 + i] = 1
  return m
}
for (const url of ['http://192.168.0.115:8080', 'https://sala-de-cine.local:8443', 'http://10.0.0.5:8080/' + 'x'.repeat(60), 'http://192.168.1.20:9000/' + 'abcdefghij'.repeat(12)]) {
  const r = await J('GET', '/api/v1/admin/servidor/qr?url=' + encodeURIComponent(url))
  const ref = QRCode.create([{ data: url, mode: 'byte' }], { errorCorrectionLevel: 'M' }) // modo byte único (el C++ no segmenta)
  const esperada = Array.from({ length: ref.modules.size }, (_, y) => Array.from({ length: ref.modules.size }, (_, x) => ref.modules.data[y * ref.modules.size + x] ? 1 : 0))
  const mia = r.s === 200 ? matrizDeSvg(r.j.svg) : null
  check('QR ' + url.slice(0, 40) + ' (v' + ref.version + ', ' + ref.modules.size + ')', mia && JSON.stringify(mia) === JSON.stringify(esperada), mia ? 'matriz distinta' : 'estado ' + r.s)
}
check('QR rechaza direcciones inválidas', (await J('GET', '/api/v1/admin/servidor/qr?url=javascript:alert(1)')).s === 400)

// ---- «Reproduciendo ahora»: un cliente con la API key pide Range del archivo y aparece en /admin/sesiones
{
  await J('PUT', '/api/v1/admin/servidor/config', { puertoHttp: 8098, protocolo: 'http', nombre: 'Sala de prueba', mdns: true })
  const est = await J('POST', '/api/v1/admin/servidor/iniciar')
  check('servidor público inicia', est.s === 200 && est.j.estado.activo, JSON.stringify(est.j))
  const key = (await J('GET', '/api/v1/admin/api-key')).j.valor
  const lib = (await J('GET', '/api/v1/libraries')).j.find(l => l.tipo === 'pelicula')
  const peli = (await J('GET', '/api/v1/libraries/' + lib.id + '/items')).j[3]
  check('sin actividad al empezar', (await J('GET', '/api/v1/admin/sesiones')).j.directas.length === 0)
  const r = await fetch('http://127.0.0.1:8098/api/v1/media/' + peli.id + '/stream', { headers: { 'X-Api-Key': key, Range: 'bytes=0-99', 'User-Agent': 'Dart/3.4 (dart:io)' } }); await r.arrayBuffer()
  const ses = (await J('GET', '/api/v1/admin/sesiones')).j
  check('aparece la reproducción directa', ses.directas.length === 1 && ses.directas[0].titulo === peli.titulo && ses.directas[0].quien === 'Token de acceso' && /^CinemaLove \(Android\) · /.test(ses.directas[0].cliente), JSON.stringify(ses))
  await fetch('http://127.0.0.1:8098/api/v1/media/' + peli.id + '/stream', { headers: { 'X-Api-Key': key, Range: 'bytes=100-199' } }).then(x => x.arrayBuffer())
  check('misma reproducción = una sola fila', (await J('GET', '/api/v1/admin/sesiones')).j.directas.length === 1)

  // ---- mDNS: una implementación independiente (bonjour-service) tiene que ver el anuncio del servidor C++
  const { Bonjour } = req('bonjour-service')
  const bj = new Bonjour()
  const visto = await new Promise((res) => { const b = bj.find({ type: 'cinemalove', protocol: 'tcp' }); b.on('up', (s) => res(s)); setTimeout(() => res(null), 8000) })
  check('bonjour-service ve el anuncio', !!visto, 'no se vio')
  if (visto) {
    check('TXT id/name/tls/v', visto.txt?.name === 'Sala de prueba' && visto.txt?.tls === '0' && visto.txt?.v === '1' && /^[0-9a-f-]{8,}$/i.test(visto.txt?.id ?? ''), JSON.stringify(visto.txt))
    check('puerto anunciado', visto.port === 8098, String(visto.port))
    check('dirección IPv4 anunciada', (visto.addresses ?? []).some(a => /^\d+\.\d+\.\d+\.\d+$/.test(a)), JSON.stringify(visto.addresses))
  }
  check('estado anunciando=true', (await J('GET', '/api/v1/admin/servidor')).j.estado.anunciando === true)

  // El cliente C++ (descubrimiento propio) ve lo mismo, y también un servicio publicado por la otra implementación
  const cli = await new Promise((res, rej) => { const p = spawn(exe, ['--client-serve', S + '/cli'], { env: { ...process.env, PATH }, stdio: ['pipe', 'pipe', 'inherit'] }); let b = ''; p.stdout.on('data', d => { b += d; const m = /CL_READY (.*)/.exec(b); if (m) res({ p, ...JSON.parse(m[1]) }) }) })
  const desc = async () => (await (await fetch(cli.baseUrl + '/control/descubrir', { headers: { 'X-Api-Key': cli.token } })).json()).servidores
  let lista = []
  for (let i = 0; i < 40 && !lista.some(x => x.puerto === 8098); i++) { lista = await desc(); await new Promise(r => setTimeout(r, 250)) }
  const mio = lista.find(x => x.puerto === 8098)
  check('el cliente C++ descubre el servidor C++', !!mio && mio.nombre === 'Sala de prueba' && mio.tls === false && /^\d+\.\d+\.\d+\.\d+$/.test(mio.host), JSON.stringify(lista))
  const pub = bj.publish({ name: 'Otro cine', type: 'cinemalove', protocol: 'tcp', port: 9123, txt: { v: '1', id: 'abc-123', name: 'Otro cine', tls: '1' } })
  for (let i = 0; i < 60 && !lista.some(x => x.puerto === 9123); i++) { lista = await desc(); await new Promise(r => setTimeout(r, 250)) }
  const otro = lista.find(x => x.puerto === 9123)
  check('el cliente C++ descubre un anuncio de bonjour-service', !!otro && otro.id === 'abc-123' && otro.nombre === 'Otro cine' && otro.tls === true, JSON.stringify(lista))
  pub.stop?.()
  await fetch(cli.baseUrl + '/control/descubrir/detener', { method: 'POST', headers: { 'X-Api-Key': cli.token } })
  cli.p.kill()
  // al detener el servidor desaparece el anuncio (aviso de despedida)
  await J('POST', '/api/v1/admin/servidor/detener')
  check('estado anunciando=false al detener', (await J('GET', '/api/v1/admin/servidor')).j.estado.anunciando === false)
  await new Promise((res) => bj.destroy(() => res()))
}

// ---- HTTPS con certificado autofirmado propio, validado por node:crypto (implementación independiente)
{
  const crypto = await import('node:crypto'); const https = await import('node:https'); const fs = await import('node:fs')
  const cfg = await J('PUT', '/api/v1/admin/servidor/config', { protocolo: 'https', puertoHttps: 8449, nombre: 'Sala segura', mdns: false })
  check('config https', cfg.s === 200 && cfg.j.config.protocolo === 'https', JSON.stringify(cfg.j))
  const t0 = Date.now()
  const ini = await J('POST', '/api/v1/admin/servidor/iniciar')
  console.log('  (generar certificado + arrancar: ' + (Date.now() - t0) + ' ms)')
  check('servidor https inicia', ini.s === 200 && ini.j.estado.activo && ini.j.estado.protocolo === 'https' && ini.j.estado.urls.every(u => u.startsWith('https://')), JSON.stringify(ini.j))
  const huella = ini.j.estado.huella
  check('huella con formato AB:CD', /^([0-9A-F]{2}:){31}[0-9A-F]{2}$/.test(huella ?? ''), String(huella))
  const key = (await J('GET', '/api/v1/admin/api-key')).j.valor
  const pedir = (path, opts = {}) => new Promise((res, rej) => { const r = https.request({ host: '127.0.0.1', port: 8449, path, rejectUnauthorized: false, headers: { 'X-Api-Key': key, ...(opts.headers ?? {}) } }, (resp) => { const ch = []; const cert = resp.socket.getPeerX509Certificate(), proto = resp.socket.getProtocol(); resp.on('data', d => ch.push(d)); resp.on('end', () => res({ s: resp.statusCode, h: resp.headers, b: Buffer.concat(ch), cert, proto })) }); r.on('error', rej); r.end() })
  const h = await pedir('/api/v1/health')
  check('health por HTTPS', h.s === 200 && JSON.parse(h.b).status === 'ok', String(h.s))
  check('TLS 1.2 o superior', ['TLSv1.2', 'TLSv1.3'].includes(h.proto), h.proto)
  const c = h.cert
  check('la firma del certificado es válida', c.verify(c.publicKey))
  check('huella = SHA-256 del DER', c.fingerprint256 === huella, c.fingerprint256 + ' vs ' + huella)
  check('RSA 2048', c.publicKey.asymmetricKeyDetails.modulusLength === 2048 && c.publicKey.asymmetricKeyType === 'rsa')
  check('sujeto CinemaLove', /CN=CinemaLove/.test(c.subject) && /O=CinemaLove/.test(c.subject), c.subject)
  check('SAN: localhost, nombre inválido omitido, 127.0.0.1', /DNS:localhost/.test(c.subjectAltName) && /IP Address:127\.0\.0\.1/.test(c.subjectAltName), c.subjectAltName)
  check('no es CA', c.ca === false)
  check('uso: serverAuth', (c.keyUsage ?? []).includes('1.3.6.1.5.5.7.3.1'), JSON.stringify(c.keyUsage))
  const dias = (new Date(c.validTo) - Date.now()) / 86400000
  check('vigencia ≈ 10 años', dias > 3640 && dias < 3660, String(dias))
  check('válido desde ayer', new Date(c.validFrom) < new Date())
  const crt = S + '/certs/server.crt', kf = S + '/certs/server.key'
  check('archivos certs/server.crt y .key', fs.existsSync(crt) && fs.existsSync(kf))
  check('la clave privada corresponde al certificado', c.checkPrivateKey(crypto.createPrivateKey(fs.readFileSync(kf))))
  check('el certificado servido es el guardado', new crypto.X509Certificate(fs.readFileSync(crt)).fingerprint256 === huella)
  const st = await pedir('/api/v1/media/' + (await J('GET', '/api/v1/libraries/' + (await J('GET', '/api/v1/libraries')).j.find(l => l.tipo === 'pelicula').id + '/items')).j[3].id + '/stream', { headers: { Range: 'bytes=0-999' } })
  check('streaming Range por HTTPS', st.s === 206 && st.b.length === 1000, st.s + ' ' + st.b.length)
  check('sin credencial → 401', (await new Promise((res) => https.get({ host: '127.0.0.1', port: 8449, path: '/api/v1/libraries', rejectUnauthorized: false }, (r) => { r.resume(); res(r.statusCode) }))) === 401)
  check('un cliente que valida el certificado lo rechaza (autofirmado)', await new Promise((res) => { const r = https.get({ host: '127.0.0.1', port: 8449, path: '/api/v1/health' }, () => res(false)); r.on('error', () => res(true)) }))
  check('cambiar el puerto con el servidor activo → 409', (await J('PUT', '/api/v1/admin/servidor/config', { puertoHttps: 8450 })).s === 409)

  // cliente C++ (Modo Cliente): sondea, confía en la huella y se conecta por HTTPS
  const cli = await new Promise((res) => { const p = spawn(exe, ['--client-serve', S + '/cli2'], { env: { ...process.env, PATH }, stdio: ['pipe', 'pipe', 'inherit'] }); let b = ''; p.stdout.on('data', d => { b += d; const m = /CL_READY (.*)/.exec(b); if (m) res({ p, ...JSON.parse(m[1]) }) }) })
  const C = async (path, body) => (await (await fetch(cli.baseUrl + path, { method: body ? 'POST' : 'GET', headers: { 'X-Api-Key': cli.token, 'Content-Type': 'application/json' }, body: body ? JSON.stringify(body) : undefined })).json())
  const sondeo = await C('/control/probar', { url: 'https://127.0.0.1:8449' })
  check('el cliente C++ ve «certificado no confiable» con la misma huella', sondeo.tipo === 'certificado-no-confiable' && sondeo.huella === huella, JSON.stringify(sondeo))
  const mal = await C('/control/conectar', { url: 'https://127.0.0.1:8449', credencial: { tipo: 'apiKey', apiKey: key }, recordar: true })
  check('sin confiar no conecta', mal.ok === false, JSON.stringify(mal))
  const bien = await C('/control/conectar', { url: 'https://127.0.0.1:8449', credencial: { tipo: 'apiKey', apiKey: key }, recordar: true, confiarHuella: huella })
  check('confiando en la huella conecta', bien.ok === true && bien.estado?.tipo === 'conectada', JSON.stringify(bien).slice(0, 300))
  if (bien.ok) {
    const lib = await (await fetch(bien.estado.api.baseUrl + '/api/v1/libraries', { headers: { 'X-Api-Key': bien.estado.api.token } })).json()
    check('la API remota por HTTPS llega por el proxy del cliente', Array.isArray(lib) && lib.length > 0)
  }
  // regenerar: la huella cambia y el cliente lo detecta
  const reg = await J('POST', '/api/v1/admin/servidor/certificado/regenerar')
  check('regenerar certificado', reg.s === 200 && reg.j.estado.activo && reg.j.estado.huella && reg.j.estado.huella !== huella, JSON.stringify(reg.j).slice(0, 200))
  const nueva = reg.j.estado.huella
  const h2 = await pedir('/api/v1/health')
  check('el servidor reiniciado presenta la huella nueva', h2.s === 200 && h2.cert.fingerprint256 === nueva)
  const rec = await C('/control/reintentar', {})
  const estCli = (await C('/control/estado')).conexion
  check('el cliente detecta el certificado cambiado', estCli.tipo === 'certificado-cambiado' && estCli.huellaNueva === nueva, JSON.stringify(estCli).slice(0, 300))
  cli.p.kill()
  await J('POST', '/api/v1/admin/servidor/detener')
}

console.log(`Resultado: ${ok} correctas, ${fail} con fallos`)
parar()
await new Promise(r => setTimeout(r, 1000)); try { rmSync(S, { recursive: true, force: true }) } catch {}
process.exit(fail ? 1 : 0)
