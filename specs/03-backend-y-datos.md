# 03 · Backend y datos

La interfaz Qt es el cliente de la API v1 (`specs/heredadas/04`). **El backend es código C++ propio, integrado en la aplicación**: no hay proceso hijo, ni Node, ni dependencias de JavaScript. (Hasta el 18/11/2026 existió un sidecar Node en `backend/`; sirvió de oráculo para comprobar la paridad y se retiró. Sigue disponible en la etiqueta git `con-backend-node`.)

## 1. Piezas

| Carpeta | Qué hace |
|---|---|
| `src/server/` | Servidor HTTP propio sobre `QTcpServer` / `QSslServer` (`HttpServer`), API v1 (`ApiServer`, `UserApi`, `ImageApi`, `AdminApi`), base de datos `Store` (QtSql, conexión por hilo, respaldo, bloqueo de un solo escritor), catálogo (`Catalog`), escáner (`Scanner`, `Naming`), TMDb (`Metadata`), trabajos con SSE (`Jobs`), contraseñas bcrypt (`Bcrypt`) |
| `src/server/PublicServer.*` | «Compartir mi colección en la red»: HTTP o HTTPS en `0.0.0.0` sin administración; certificado autofirmado propio (`Certs`: RSA 2048, SHA-256, 10 años, huella SHA-256), anuncio mDNS `_cinemalove._tcp` (`Mdns`), código QR (`Qr`) y «Reproduciendo ahora» (`Activity`) |
| `src/client/` | Modo Cliente: conexión con otro CinemaLove (`ClientConnection`), proxy local con la credencial remota (`ClientProxy`), espejo local del catálogo (`Mirror`), progreso, «Próximas a ver» y colecciones de este equipo (`Personal`, `ClientState`), `cliente_datos.sqlite` (`ClientDb`), servidor de control `/control/*` (`ClientBackend`) |
| `src/backend/BackendLauncher.*` | Ciclo de vida: abre la colección y levanta la API interna (Modo Local) o el servicio del Modo Cliente; se cierra con la app |

Todo cálculo criptográfico (bignum, RSA, ASN.1/X.509, bcrypt) y el codificador QR son código del proyecto; Qt aporta TLS (Schannel en Windows, OpenSSL en Linux) y el resto de la red.

## 2. Ciclo de vida
1. Al arrancar: el asistente de bienvenida elige la carpeta de datos; `Store::open` toma el bloqueo de un solo escritor (`cinemalove.electron.lock` + `cinemalove.qt.lock`, por PID), hace el respaldo automático (`cinemalove.sqlite.bak-qt-AAAAMMDD`, cada 7 días, conserva 5) y migra de forma aditiva. La apertura se hace fuera del hilo de la interfaz.
2. La API interna escucha en `127.0.0.1` con puerto efímero y un token de administración aleatorio de la sesión; vive en su propio hilo.
3. En Modo Cliente no hay colección local: el servicio de control (`/control/*`, token efímero) gestiona la conexión, el proxy y el espejo; una vez conectado, la interfaz habla con el proxy local.
4. Se cierra con la app (`aboutToQuit`). Los datos nunca se pierden por esto (SQLite transaccional).

## 3. Datos y compatibilidad
Sin cambios: `cinemalove.sqlite` (drift v11, solo cambios aditivos), `cliente_datos.sqlite` (esquema v6 + `cl_cliente_servidor_extra`), `images/`, `config.yaml` (`specs/heredadas/02`). La API HTTP v1 sigue siendo compatible con los clientes Flutter. Mientras coexistan otras versiones de CinemaLove, **no pueden abrir la misma carpeta a la vez** (un solo escritor).

Durante el desarrollo se trabaja con **copias** en `.dev-data/`; las pruebas automatizadas se niegan a usar otra cosa. `cinemalove.exe --serve <carpeta .dev-data>` expone solo la API (sin interfaz) y `--client-serve <carpeta .dev-data>` el servicio del Modo Cliente, para probarlos con cualquier cliente HTTP.

## 4. Verificación hecha al retirar Node
El comportamiento se comparó con el del backend Node sobre copias idénticas de los datos: API, escáner, metadata TMDb, usuarios y sesiones, imágenes y administración (1374 comprobaciones iguales); Modo Cliente (29: conexión con token y usuario, espejo, proxy con lo personal, streaming Range, reconexión, sesión vencida, servidor caído); servidor público (45: QR idéntico a la biblioteca de referencia, certificado validado por `node:crypto`, mDNS cruzado con `bonjour-service`, HTTPS con Range y detección del certificado regenerado). Los scripts de esas pruebas están en la etiqueta `con-backend-node` (`scripts/paridad-*.mjs`).

## 5. Conversión de video
El backend integrado no transcodifica (no hay sesiones ffmpeg): la UI Qt reproduce el archivo original con mpv y los clientes que piden `media/<id>/stream` reciben el archivo con soporte de `Range`.
